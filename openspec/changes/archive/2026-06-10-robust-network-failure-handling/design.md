## Context

The logger task (`logger_task`, core 0, priority 4) currently performs sensor event processing, SD card writes, and synchronous WiFi+MQTT publishing in a single thread. Each `PublishParameters()` or `PublishFailure()` call executes the full connect-publish-disconnect cycle: WiFi connect (20s timeout), NTP sync (15s timeout), MQTT connect (10s timeout), publish, stop, disconnect. When WiFi or MQTT broker is unreachable, each publish attempt blocks up to 45 seconds. The task retries immediately on each 100ms loop tick with no backoff, effectively freezing event processing.

Failure events write to SD **after** the MQTT attempt, risking data loss if the node loses power during the blocking network call. The 32-slot pending parameter buffer wraps silently during extended outages.

The existing change `fix-mqtt-client-state-on-error` addresses a `LoadProhibited` crash when `EnsureMqttClient()` leaves `s_client` non-null after a mid-init failure. That fix is subsumed here — the MQTT client lifecycle is redesigned as part of the network task.

Dashboard mode (`CONFIG_DASHBOARD_ENABLE`) and field mode have fundamentally different WiFi strategies scattered across `#if` guards in `logger_mqtt.cpp`.

## Goals / Non-Goals

**Goals:**

- Logger task processes events and writes SD with bounded latency (≤10ms), regardless of network state.
- Network failures do not block sensor event processing or SD storage.
- Failure events (free fall, acoustic emission) reach SD within one task loop tick (~100ms).
- Unsent MQTT payloads persist on SD and are uploaded on reconnect — zero data loss during outages.
- Exponential backoff prevents wasted radio time and power during sustained outages.
- Dashboard and field WiFi strategies are cleanly separated at compile time.
- `EnsureMqttClient()` crash from `fix-mqtt-client-state-on-error` is fixed as part of client lifecycle.

**Non-Goals:**

- Changing MQTT topic structure, payload formats, or QoS settings.
- Implementing deep sleep or advanced power management.
- Adding OTA or remote configuration.
- Changing the monitor task or its event emission patterns.
- WiFi roaming, multi-AP, or mesh networking.

## Decisions

### 1. Dedicated network task for all WiFi/MQTT operations

All WiFi connect, NTP sync, MQTT connect, publish, and disconnect operations move to a new `network_task` (core 0, priority 3 — below logger). The logger task enqueues publish requests via a FreeRTOS queue; the network task drains it.

```
BEFORE                              AFTER
══════                              ═════

┌──────────────┐               ┌──────────────┐    ┌──────────────┐
│ logger_task  │               │ logger_task  │    │ network_task │
│              │               │              │    │              │
│ event recv   │               │ event recv   │    │ WiFi connect │
│ SD write     │               │ SD write     │    │ NTP sync     │
│ WiFi connect │ ← blocking    │ queue publish │──▶│ MQTT publish │
│ MQTT publish │               │              │    │ SD queue scan│
│ WiFi stop    │               │              │    │ backoff mgmt │
└──────────────┘               └──────────────┘    └──────────────┘
                                 always fast         blocks OK here
```

**Alternative:** Use a semaphore to make the logger task skip publish when WiFi is known-down. Rejected — still couples network awareness into the logger, and doesn't solve the SD upload queue requirement.

**Alternative:** Async MQTT with callbacks. Rejected — ESP-IDF MQTT client's async model still requires `client_start`/`client_stop` synchronously, and callback-based flow adds complexity without eliminating the WiFi connect blocking.

### 2. SD-backed upload queue

Structure on SD:

```
/sdcard/
  outbox/
    pending/        ← files waiting to be sent
      params_1718033400.jsonl
      params_1718037000.jsonl
      failure_1718033412.jsonl
    sent/           ← files successfully sent (pruned periodically)
```

**Logger task writes:** On each parameter/failure event, the logger formats JSON and appends to the current `pending/params_<epoch>.jsonl` file. A new file is created each publish period (matching `CONFIG_LOGGER_WIFI_PERIOD_HOURS`). Failure events get individual files (`failure_<epoch>.jsonl`) for priority handling.

**Network task reads:** Scans `pending/`, publishes file contents line-by-line to MQTT, moves completed files to `sent/`. Failure files are prioritized over parameter files in scan order. The `sent/` directory is pruned when it exceeds a configurable threshold (default: keep last 10 files).

**Alternative:** In-memory ring buffer with larger capacity. Rejected — still finite, doesn't survive reboot, ESP32-S3 RAM budget is tight (~200KB free post-boot).

**Alternative:** SQLite or key-value store on SD. Rejected — overkill for append-only JSONL; file-per-batch is simpler and maps directly to the publish granularity.

### 3. Exponential backoff with jitter

```cpp
struct BackoffState {
    std::uint32_t current_ms{kBackoffInitialMs};  // 60000 (1 min)
    std::uint64_t backoff_until_us{0};
    
    static constexpr std::uint32_t kBackoffInitialMs = 60000U;
    static constexpr std::uint32_t kBackoffMaxMs = 3600000U;  // 1 hour
    static constexpr std::uint32_t kBackoffJitterMs = 5000U;
    
    bool ShouldSkip(std::uint64_t now_us) const;
    void OnFailure(std::uint64_t now_us);
    void OnSuccess();
};
```

On WiFi connect failure: `current_ms = min(current_ms * 2, kBackoffMaxMs)`, set `backoff_until_us`. On success: reset to `kBackoffInitialMs`. Jitter via `esp_random() % kBackoffJitterMs` prevents thundering herd if multiple nodes share an AP.

The network task checks `ShouldSkip()` before each publish attempt. While in backoff, pending data accumulates on SD.

**Alternative:** Fixed retry interval. Rejected — wastes power at the same rate during 5-minute vs 5-hour outages.

### 4. Compile-time network strategy separation

Two source files, one linked per config:

```
components/logger/
  network_persistent.cpp    ← CONFIG_DASHBOARD_ENABLE=y
  network_on_demand.cpp     ← CONFIG_DASHBOARD_ENABLE=n
```

Shared interface in `network_strategy.hpp`:

```cpp
namespace logger::network {
    bool Init() noexcept;
    bool EnsureConnected() noexcept;   // persistent: check bit; on-demand: full connect
    void ReleaseConnection() noexcept; // persistent: no-op; on-demand: disconnect+stop
    bool IsConnected() noexcept;       // check event group bit
}
```

CMakeLists selects the source file:

```cmake
if(CONFIG_DASHBOARD_ENABLE)
    list(APPEND SRCS "network_persistent.cpp")
else()
    list(APPEND SRCS "network_on_demand.cpp")
endif()
```

**Alternative:** Runtime polymorphism via virtual interface. Rejected — vtable dispatch adds indirection on a hot path; compile-time selection has zero runtime cost and the config is fixed at build time.

**Alternative:** Single source with `if constexpr`. Rejected — the two strategies differ substantially in lifecycle (auto-reconnect event handler vs manual connect/disconnect); separate files are clearer.

### 5. MQTT client lifecycle cleanup (absorbing fix-mqtt-client-state-on-error)

`EnsureMqttClient()` moves into the network task. All failure paths after `esp_mqtt_client_init()` call `esp_mqtt_client_destroy(s_client)` and set `s_client = nullptr`. This is the same fix as `fix-mqtt-client-state-on-error` but integrated into the new module structure.

Additionally, the network task manages client lifecycle more cleanly: create once at task init, reuse across publishes within a connection, destroy only on unrecoverable error or task shutdown. The current create-per-publish pattern is maintained for on-demand mode but the handle is properly cleaned on any failure path.

### 6. Failure event priority path

Logger task reorders failure handling:

```
BEFORE:                          AFTER:
  mqtt::PublishFailure()           storage::AppendFailure()   ← SD first, always
  storage::AppendFailure()         EnqueueToNetwork(failure)  ← async, best-effort
```

Failure JSONL files in `pending/` are named with `failure_` prefix. Network task sorts `pending/` contents with `failure_*` files first, ensuring they're published before parameter batches when connectivity resumes.

## Risks / Trade-offs

- **SD write throughput**: Every parameter event now writes to SD (was only per-batch before). At ~1 event per 5 min in IDLE, negligible. During DISTURBED (~1 event per 2s), still well within SD write bandwidth (~1 KB/s vs SD capability of ~1 MB/s). → Acceptable.

- **SD wear**: Continuous small writes to FAT filesystem. Mitigated by `allocation_unit_size = 16KB` already configured. At worst-case DISTURBED rate (1 write/2s), decades of lifetime on any modern SD card. → Acceptable.

- **Outbox directory management**: `pending/` could accumulate many files during long outages. Mitigated by file-per-period granularity (1 file per `CONFIG_LOGGER_WIFI_PERIOD_HOURS`, default 6h). A week-long outage = ~28 files. → Acceptable. The `sent/` pruning threshold prevents unbounded growth post-reconnect.

- **Task stack size**: New `network_task` needs stack for WiFi+TLS+MQTT. Current logger task was 4KB; network task will need ~6KB for TLS handshake. → Budget verified against post-boot heap diagnostics (~200KB+ free).

- **Reboot during outage**: Unsent data survives on SD in `pending/`. Network task scans on startup. No data loss. → Strength, not risk.

- **Clock skew**: If NTP sync fails, JSONL filenames use monotonic counter or boot-relative timestamp. Server can correlate via payload `timestamp_us`. → Acceptable.
