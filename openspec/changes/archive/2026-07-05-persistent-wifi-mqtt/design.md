## Context

The logger component already ships two link-time WiFi strategies behind a shared `network_strategy.hpp` interface:

- `network_on_demand.cpp` — full WiFi connect/disconnect per publish cycle (field mode). Has bounded retries, transient-reason classification, and bounded cleanup.
- `network_persistent.cpp` — WiFi stays up with auto-reconnect (dashboard mode). `EnsureConnected()` checks an event-group bit; `ReleaseConnection()` is a no-op.

Selection today is driven by `CONFIG_DASHBOARD_ENABLE` in `components/logger/CMakeLists.txt:5-8`. The persistent impl has two defects that matter for a demo:

1. `network_persistent.cpp:86` calls `esp_wifi_connect()` synchronously on every `WIFI_EVENT_STA_DISCONNECTED`. Under a flaky AP this hot-loops with no delay and no cap, burning energy and flooding the log.
2. `network_task.cpp:259` sets `mqtt_cfg.network.disable_auto_reconnect = true`, and the task tears the MQTT client down (`esp_mqtt_client_stop()` at `network_task.cpp:472`) plus WiFi (`network::ReleaseConnection()` at `network_task.cpp:490`) at the end of every publish cycle. In persistent mode this defeats the purpose: MQTT never self-heals, and the task relies on the next cadence wake to re-establish MQTT.

`CONFIG_ESP_PM_ENABLE` is unset and `esp_wifi_set_ps()` is never called. The monitor runs IMU at 52 Hz and the AE spectral ADC continuously on core 1, so the CPU baseline is fixed regardless of WiFi strategy. Target hardware is ESP32-S3FH4R2 on the custom PCB defined in `main/pins.hpp`.

Power backdrop (radio-only delta over the fixed monitor baseline, ESP32-S3 datasheet typical):

```
Strategy                   WiFi-active    WiFi-idle      Avg WiFi delta
─────────────────────────────────────────────────────────────────────
On-demand (1h, ~10s/cycle)  ~100 mA ×10s   0 mA ×3590s    ~0.3-0.5 mA
Persistent (no PM)          —              ~20 mA cont.   ~15-25 mA
```

For a multi-hour demo the extra ~20 mA is irrelevant. For week-long field deployment it is not — but persistent mode is opt-in, so on-demand stays the field default.

## Goals / Non-Goals

**Goals:**
- Make persistent WiFi+MQTT selectable independent of the HTTP dashboard.
- Bound every reconnect occurrence with a single backoff policy and a clear owner per occurrence.
- Keep the demo link stable without per-publish reconnect latency.
- Preserve on-demand mode behavior exactly.
- Stay allocation-free on the reconnect path and keep the WiFi event handler deadlock-free.

**Non-Goals:**
- Enabling `CONFIG_ESP_PM_ENABLE`, light-sleep, DFS, or CPU frequency scaling. Explicitly out of scope per the change request.
- Modifying the monitor sample rate or AE spectral task to enable deeper sleep.
- Changing the MQTT topic scheme, client identity, or QoS.
- Changing the outbox pending/sent file protocol.
- Adding TLS; the broker is `mqtt://10.19.100.242:1883` (plaintext LAN).
- Runtime switching between on-demand and persistent. Mode is compile-time.

## Decisions

### Decision 1: New `CONFIG_LOGGER_NETWORK_MODE` Kconfig choice, dashboard only implies persistent

Replace the `CONFIG_DASHBOARD_ENABLE` boolean in `components/logger/CMakeLists.txt` with a Kconfig `choice` named `LOGGER_NETWORK_MODE` with values `on-demand` and `persistent`. When `CONFIG_DASHBOARD_ENABLE=y` and the choice is untouched, Kconfig defaults it to `persistent` via a `default` conditional. The dashboard component keeps building only when `CONFIG_DASHBOARD_ENABLE=y`.

**Alternatives considered:**
- Keep `CONFIG_DASHBOARD_ENABLE` as the selector. Rejected: couples two concerns (HTTP server presence vs. radio policy) and blocks persistent-without-dashboard builds.
- Add a second boolean `CONFIG_LOGGER_NETWORK_PERSISTENT`. Rejected: two booleans can disagree; a `choice` is mutually exclusive and self-documenting.

### Decision 2: Persistent WiFi backoff state machine in `network_persistent.cpp`

Add a file-scope `BackoffState` (mirrors the shape already in `network_task.cpp:62-92`) with `initial_ms`, `max_ms`, `jitter_ms`, and a `next_attempt_us` deadline. The disconnect event handler does **not** call `esp_wifi_connect()` directly. Instead it records that a reconnect is due and schedules a one-shot `esp_timer` callback (or posts to the existing event group and lets `EnsureConnected()` / a lightweight reconnect timer task perform the connect). The first connect on boot uses `initial_ms`; each failure doubles the interval up to `max_ms`; a successful `IP_EVENT_STA_GOT_IP` resets to `initial_ms`.

Backoff state is plain `static` POD. No heap, no mutex. The event handler only touches it via the event group plus a single `std::uint64_t` deadline read/written atomically (32-bit aligned on ESP32-S3, atomic via `__atomic` builtins or a `portMUX_TYPE` for the few RMW ops).

**Alternatives considered:**
- Keep calling `esp_wifi_connect()` in the handler but gate it with a timestamp check. Rejected: still runs in the event loop task and races with `EnsureConnected()`; harder to reason about.
- Drive reconnect from the network task only. Rejected: the network task wakes on cadence/notify; a down link could wait up to the cadence period (1 h) before any reconnect attempt. The demo needs sub-minute recovery.

### Decision 3: Reconnect occurrence ownership table

Single source of truth for which backoff owns each occurrence:

```
#   Occurrence                         Owner                         Max interval
─── ────────────────────────────────── ───────────────────────────── ────────────
1   Boot initial WiFi connect          Persistent WiFi backoff       1 h
2   WiFi mid-session disconnect        Persistent WiFi backoff       1 h
3   WiFi EnsureConnected failure       Persistent WiFi backoff       1 h
4   MQTT mid-session disconnect        ESP-IDF MQTT auto-reconnect   configured on client
5   MQTT connect timeout in task loop  Network task backoff          1 h
```

WiFi occurrences (1-3) all share the same persistent backoff timer so the radio is never double-retried. MQTT occurrences (4-5) are split: mid-session drops are healed by the IDF client's own reconnect (no task involvement), while a fresh `esp_mqtt_client_start()` that times out in the task loop is owned by the network task backoff. The network task backoff is **not** applied to WiFi events in persistent mode.

### Decision 4: MQTT auto-reconnect on in persistent mode, off in on-demand mode

`EnsureMqttClient()` reads a compile-time flag from the selected strategy header (e.g. `logger::network::kAutoReconnect`) and sets `mqtt_cfg.network.disable_auto_reconnect` plus `mqtt_cfg.network.reconnect_timeout_ms` accordingly. In persistent mode: `disable_auto_reconnect = false`, `reconnect_timeout_ms` bounded (e.g. 10 s). In on-demand mode: current behavior preserved (`true`).

The network task publish loop branches on the same flag: in persistent mode it skips `esp_mqtt_client_stop()`, `network::ReleaseConnection()`, and the per-cycle NTP sync. It still calls `EnsureMqttClient()` and waits for `MQTT_EVENT_CONNECTED`; if the client is already connected the wait returns immediately.

**Alternatives considered:**
- Always enable MQTT auto-reconnect. Rejected: in on-demand mode WiFi is torn down between cycles, so an auto-reconnecting MQTT client would spin against a dead link and burn power.
- Recreate the MQTT client each cycle in persistent mode. Rejected: destroys the always-on contract and adds latency.

### Decision 5: Explicit `esp_wifi_set_ps(WIFI_PS_NONE)` in persistent init

Call `esp_wifi_set_ps(WIFI_PS_NONE)` once during `InitWifiCore()` in `network_persistent.cpp` before the first `esp_wifi_start()`. `CONFIG_ESP_PM_ENABLE` stays unset. This makes the no-PM stance explicit instead of relying on the IDF default, and matches the "no PM" scope of the change.

### Decision 6: CMake selection by the new choice

`components/logger/CMakeLists.txt` switches:

```cmake
if(CONFIG_LOGGER_NETWORK_MODE_PERSISTENT)
    list(APPEND SRCS "network_persistent.cpp")
else()
    list(APPEND SRCS "network_on_demand.cpp")
endif()
```

Kconfig symbol naming follows IDF convention: the choice value `persistent` produces `CONFIG_LOGGER_NETWORK_MODE_PERSISTENT=y`.

## Risks / Trade-offs

- **[Power: +15-25 mA WiFi idle vs ~0.4 mA on-demand]** → Accepted for demo. Documented in proposal/design. On-demand remains the field default. No PM means no light-sleep recovery; the monitor 52 Hz + AE ADC baseline dominates anyway.
- **[Reconnect storm under flaky AP]** → Bounded by the persistent backoff cap. The event handler no longer hot-loops. Worst case is one connect attempt per `max_ms`.
- **[MQTT auto-reconnect racing the task loop]** → The task loop only calls `esp_mqtt_client_start()` if the client is not already started; it waits on the connected bit regardless. Auto-reconnect handles mid-session drops; the task loop only owns fresh-start timeouts. The `MQTT_EVENT_CONNECTED` / `DISCONNECTED` handler already clears/sets the bit atomically.
- **[Event handler concurrency with `EnsureConnected()`]** → Backoff state is static POD; RMW guarded by a `portMUX_TYPE` (IRAM-safe) or `__atomic` builtins. The event handler never takes a mutex that `EnsureConnected()` holds. Event group bits are the only sync primitive shared with the task.
- **[`esp_timer` one-shot for reconnect vs. event-group-only]** → If a timer is used, it must be created once at init and armed with `esp_timer_start_once()`. Timer callback runs in the esp_timer task, not the event loop, so calling `esp_wifi_connect()` there is safe. Trade-off: one extra static timer handle.
- **[sdkconfig migration]** → Existing `sdkconfig*` files do not set `CONFIG_LOGGER_NETWORK_MODE*`. Unset choice + `CONFIG_DASHBOARD_ENABLE=y` must default to persistent to keep the dashboard build green. `Kconfig.defaults` encodes that default.
- **[Two backoff timers with similar shape]** → The persistent WiFi backoff and the network task backoff are intentionally separate because they own different occurrences (table above). Sharing one struct type (copy-paste) is fine; sharing one *instance* is not.

## Migration Plan

1. Add the `choice LOGGER_NETWORK_MODE` to the logger Kconfig (new file `components/logger/Kconfig` or extend the project Kconfig).
2. Set defaults: `CONFIG_LOGGER_NETWORK_MODE_PERSISTENT=y` when `CONFIG_DASHBOARD_ENABLE=y`; `CONFIG_LOGGER_NETWORK_MODE_ON_DEMAND=y` otherwise. Encode in `Kconfig.defaults` / `sdkconfig.defaults`.
3. Update all `sdkconfig*` files in the repo root and any `build-*` presets that pin symbols to set the new choice explicitly.
4. Update `components/logger/CMakeLists.txt` to the new conditional.
5. Build both variants (`CONFIG_LOGGER_NETWORK_MODE=persistent` and `=on-demand`) and run the existing unity tests in `components/monitor/test/`.
6. Rollback: revert the CMakeLists conditional and Kconfig addition; the old `CONFIG_DASHBOARD_ENABLE` boolean path is restored.

## Open Questions

- **Persistent WiFi backoff constants**: initial 5 s, max 1 h, jitter 5 s? (On-demand uses 60 s initial / 1 h max / 5 s jitter via the network task. Persistent should recover faster for the demo — propose 5 s initial, 5 min max, 2 s jitter. Needs sign-off.)
- **MQTT reconnect interval**: 10 s is a reasonable default for LAN; confirm against the broker's keepalive config.
- **Reconnect driver**: `esp_timer` one-shot vs. a dedicated lightweight FreeRTOS task vs. piggybacking on the network task wake. Lean toward `esp_timer` one-shot for minimal footprint; finalize in tasks.
- **Should `EnsureConnected()` in persistent mode proactively arm a reconnect when down-and-backoff-elapsed, or only react to the timer callback?** Lean: timer callback performs the connect; `EnsureConnected()` only reports state.
