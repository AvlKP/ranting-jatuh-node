## 1. Kconfig & build selection

- [x] 1.1 Add a Kconfig `choice LOGGER_NETWORK_MODE` with values `on-demand` and `persistent` to the logger component Kconfig (create `components/logger/Kconfig` if none exists).
- [x] 1.2 Set defaults so `CONFIG_DASHBOARD_ENABLE=y` implies `CONFIG_LOGGER_NETWORK_MODE_PERSISTENT=y` and otherwise defaults to `CONFIG_LOGGER_NETWORK_MODE_ON_DEMAND=y`; encode in `Kconfig.defaults` / `sdkconfig.defaults`.
- [x] 1.3 Update `components/logger/CMakeLists.txt` strategy conditional from `CONFIG_DASHBOARD_ENABLE` to `CONFIG_LOGGER_NETWORK_MODE_PERSISTENT`.
- [x] 1.4 Update all repo-root `sdkconfig*` files (and any `build-*` presets that pin symbols) to set the new choice explicitly; verify `CONFIG_DASHBOARD_ENABLE=y` builds still select persistent.
- [x] 1.5 Confirm `CONFIG_ESP_PM_ENABLE` remains unset and document the no-PM stance in `components/logger/README.md` (or add a note to the existing docs).

## 2. Persistent WiFi backoff & no-PS

- [x] 2.1 Add a file-scope `BackoffState` POD to `network_persistent.cpp` (initial 5 s, max 5 min, jitter 2 s — per design open question; confirm before merge) with `next_attempt_us` deadline, doubling, and reset-on-success.
- [x] 2.2 Guard backoff RMW with a `portMUX_TYPE` (IRAM-safe) or `__atomic` builtins so the event handler and `EnsureConnected()` never race; no heap, no mutex.
- [x] 2.3 Remove the synchronous `esp_wifi_connect()` call from the `WIFI_EVENT_STA_DISCONNECTED` handler in `network_persistent.cpp:86`; instead record a reconnect-due flag and arm a one-shot `esp_timer` to perform `esp_wifi_connect()` after the backoff delay.
- [x] 2.4 Create the `esp_timer` handle once in `InitWifiCore()` (static storage); the timer callback calls `esp_wifi_connect()` and, on failure, re-arms itself with the next backoff interval.
- [x] 2.5 Reset backoff to `initial_ms` on `IP_EVENT_STA_GOT_IP` in `IpEventHandler`.
- [x] 2.6 Call `esp_wifi_set_ps(WIFI_PS_NONE)` once during `InitWifiCore()` before the first `esp_wifi_start()`.
- [x] 2.7 Update `EnsureConnected()` to return `false` without forcing a connect when the backoff window has not elapsed; keep the fast path (event-group bit check) unchanged.
- [x] 2.8 Add a header constant (e.g. `logger::network::kAutoReconnect` and `kMqttReconnectTimeoutMs`) in `network_strategy.hpp` so the network task can branch on mode without `#if` guards.
- [x] 2.9 Start persistent WiFi asynchronously from `network::Init()` so dashboard-enabled boot gets an IP before any publish-cycle work exists; `EnsureConnected()` remains a fast status/progress check.
- [x] 2.10 Make the first persistent reconnect delay use the configured initial interval (5 s + jitter), not the doubled interval.
- [x] 2.11 Treat `esp_wifi_connect()` timer failures, including `ESP_ERR_WIFI_CONN`, as retryable/rearmable unless a deliberate stop state exists.

## 3. MQTT auto-reconnect & lifecycle

- [x] 3.1 In `network_task.cpp` `EnsureMqttClient()`, read `logger::network::kAutoReconnect` and set `mqtt_cfg.network.disable_auto_reconnect` accordingly (false in persistent, true in on-demand).
- [x] 3.2 Set `mqtt_cfg.network.reconnect_timeout_ms` to a bounded value (10 s) in persistent mode; leave unset in on-demand mode.
- [x] 3.3 Keep the existing cleanup-on-init-failure paths (destroy + null the handle) unchanged for both modes.
- [x] 3.4 Avoid calling `esp_mqtt_client_start()` on an already-started persistent client after `MQTT_EVENT_DISCONNECTED`; let ESP-IDF auto-reconnect own mid-session disconnects.

## 4. Network task persistent-mode publish loop

- [x] 4.1 Branch the post-publish teardown in `TaskLoop()` on `logger::network::kAutoReconnect`: in persistent mode skip `esp_mqtt_client_stop()` and skip `network::ReleaseConnection()`.
- [x] 4.2 In persistent mode, skip the per-cycle `SyncTime()` call (NTP sync once after first WiFi connect is enough); keep on-demand behavior unchanged.
- [x] 4.3 Ensure the `MQTT_EVENT_CONNECTED` wait still works when the client is already connected (the bit must already be set so the wait returns immediately).
- [x] 4.4 Verify the network task backoff is applied only to MQTT connect timeouts in persistent mode, not to WiFi disconnect events (owner table in design.md).
- [x] 4.5 Add a log line that identifies which backoff owner fired (WiFi strategy vs. network task) so diagnostics distinguish the two.

## 5. Tests

- [x] 5.1 Add a unity test in `components/monitor/test/` that builds the persistent strategy and asserts `EnsureConnected()` returns false within the backoff window after a simulated disconnect (does not force an immediate connect).
- [x] 5.2 Add a unity test that asserts the persistent disconnect handler does not call `esp_wifi_connect()` synchronously (verify a reconnect is scheduled via the timer/backoff state instead).
- [x] 5.3 Add a unity test that asserts backoff doubles up to the cap and resets to `initial_ms` on success.
- [x] 5.4 Add a unity test that asserts `EnsureMqttClient()` sets `disable_auto_reconnect = false` and a bounded `reconnect_timeout_ms` in persistent mode and `true` in on-demand mode.
- [x] 5.5 Add a unity test that asserts the persistent publish loop does not call `esp_mqtt_client_stop()` or `network::ReleaseConnection()` on a successful cycle.
- [x] 5.6 Add a unity test covering the enumerated occurrences (boot, WiFi mid-session, WiFi EnsureConnected failure, MQTT mid-session, MQTT connect timeout) asserting the correct backoff owner for each.
- [x] 5.7 Keep existing `test_outbox_network_runtime.cpp` and `test_network_wifi_reason.cpp` green for on-demand mode.
- [x] 5.8 Replace copied test-only backoff coverage with production-path coverage or test hooks that exercise the real persistent reconnect state machine.
- [x] 5.9 Make persistent-specific owner tests compile out or invert correctly in on-demand builds so on-demand test execution can pass.
- [x] 5.10 Add coverage for dashboard-enabled persistent boot: `network::Init()` starts WiFi without waiting for outbox publish or startup verification.

## 6. Verification

- [x] 6.1 Build `CONFIG_LOGGER_NETWORK_MODE=persistent` with `CONFIG_DASHBOARD_ENABLE=y` and confirm the dashboard + persistent link come up.
- [x] 6.2 Build `CONFIG_LOGGER_NETWORK_MODE=persistent` with `CONFIG_DASHBOARD_ENABLE=n` and confirm persistent WiFi/MQTT without the HTTP server.
- [x] 6.3 Build `CONFIG_LOGGER_NETWORK_MODE=on-demand` and confirm on-demand behavior is unchanged (existing tests pass).
- [x] 6.4 Run the unity test build (`idf.py -B build-test -DTEST_COMPONENTS=monitor build` or the project's test command) and confirm all tests pass.
- [x] 6.5 On hardware, force an AP disconnect mid-session in persistent mode and confirm reconnect happens after the backoff window with no log flood.
- [x] 6.6 On hardware, confirm MQTT auto-reconnect recovers a broker kill -9 within the configured interval without network task intervention.
- [x] 6.7 Record network task and monitor task stack high-water margins in persistent mode and confirm no regression vs. on-demand.
- [x] 6.8 Run `git diff --check` and clear whitespace errors before merge.
