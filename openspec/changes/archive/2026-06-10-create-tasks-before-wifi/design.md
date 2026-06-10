## Context

The firmware boot sequence currently initializes WiFi inside `logger::mqtt::Init()` (via `StartWifiPersistent()` when `CONFIG_DASHBOARD_ENABLE` is active). This calls `esp_wifi_init()`, `esp_wifi_start()`, and `esp_wifi_connect()` — all of which allocate buffers from internal heap. With WPA2-Enterprise (EAP-TLS), mbedTLS allocates additional transient buffers for the TLS handshake, certificate parsing, and RADIUS exchange.

Even after a failed ENT authentication (e.g., 30s handshake timeout), the freed buffers leave the heap fragmented: `largest_block` (3072 bytes) is smaller than both `logger_task` (4096) and `monitor_task` (6144) stack requirements, despite 5560 bytes of total free heap remaining.

The `logger_task` and `monitor_task` do not require WiFi to operate — they sample the IMU, run DSP, queue events, and write to SD card. MQTT publishing gracefully fails and retries when WiFi is unavailable. Therefore, their stacks can be allocated before any WiFi subsystem initialization.

Boot heap diagnostics were added in a prior change and confirmed the fragmentation pattern:
```
I APP: heap post_ntp free=5560 min_free=3120 largest_block=3072
E LOGGER: Failed to create logger_task (stack=4096 free_internal=5560 largest_block=3072)
E MONITOR: Failed to create monitor_task (stack=6144 free_internal=5560 largest_block=3072)
```

## Goals / Non-Goals

**Goals:**

- Allocate `logger_task` and `monitor_task` stacks before any WiFi/ENT allocations to avoid heap fragmentation interference.
- Preserve existing behavior: WiFi configuration, MQTT setup, event handlers, and dashboard are unchanged.
- Add a heap checkpoint after critical task creation to verify the fix works for both PSK and ENT WiFi.

**Non-Goals:**

- Fix WPA2-Enterprise authentication failures themselves.
- Add PSRAM support.
- Guarantee dashboard HTTP server starts under ENT WiFi (dashboard remains optional/non-critical).
- Change the WiFi or MQTT configuration flow.

## Decisions

### 1. Split `mqtt::Init()` into `InitCore()` and `StartWifi()`

The current `Init()` unconditionally calls `StartWifiPersistent()` or `InitWifiCore()` depending on `CONFIG_DASHBOARD_ENABLE`. This couples queue/event-handler setup with WiFi driver init.

**New API:**

```
InitCore()      → InitNvs(), create event groups, register handlers (NO WiFi)
StartWifi()     → InitWifiCore(), esp_wifi_start(), esp_wifi_connect()
```

The boot sequence becomes:

```
                    ┌─────────────────────────────────────┐
                    │           app_main()                │
                    │                                     │
event_loop ─────────┤                                     │
IMU, SD init ───────┤                                     │
logger.InitCore() ──┤ → queue, events (clean heap)       │
                    │                                     │
logger.Start() ─────┤ → stack=4096, clean heap ✓          │
monitor.Start() ────┤ → stack=6144, clean heap ✓          │
                    │                                     │
LogHeapDiagnostics ─┤ → verify largest_block              │
                    │                                     │
logger.StartWifi() ─┤ → esp_wifi_init() + connect         │
                    │    (now fragments, doesn't matter)  │
SyncTimeOnce() ─────┤ → NTP sync                          │
Dashboard ──────────┤ → optional, may fail under ENT      │
                    └─────────────────────────────────────┘
```

**Alternative:** Pre-allocate a large heap block before WiFi, free it after WiFi (heap reserve trick). Rejected because it masks fragmentation rather than preventing it. The architectural fix (tasks before WiFi) is cleaner and provides better diagnostics.

### 2. Keep `InitCore()` minimal — no allocation beyond event groups

`InitCore()` will:
1. Call `InitNvs()` (idempotent)
2. Create static event groups (`xEventGroupCreateStatic`)
3. Register WiFi/IP event handlers
4. Return true

These operations allocate minimal, bounded heap (event group control blocks are static).

**Alternative:** Fold WiFi init into a separate phase entirely, removing the `Init()` call from `Logger::Init()`. Rejected because it breaks the existing API for callers that still want all-in-one init. Instead, `Logger::Init()` will call `InitCore()` and `StartWifi()` for backward compatibility, while `app_main` calls them separately.

### 3. StartWifi() reuses existing `InitWifiCore()` and `StartWifiPersistent()` logic

No new WiFi init code — just expose the existing functions through a public API. The internal logic (SSID config, ENT vs PSK path, event handler registration) is unchanged.

### 4. WiFi buffer reductions partially reverted

The prior change (`fix-ram-size-issue`) reduced WiFi dynamic RX/TX buffers from 32→16 to free heap for task stacks. With tasks now allocated before WiFi, that heap pressure is gone. Under ENT, the WiFi stack itself needs contiguous heap for beacon/probe/4-way-handshake allocations — community reports indicate failures below ~3000 bytes contiguous (reference: esp32.com/t=6663).

The `sdkconfig.defaults` dynamic buffer reductions (16) will be reverted to ESP-IDF defaults (32) to give the WiFi stack a larger contiguous pool. The static RX reduction (10→6) and LWIP recvmbox reduction (32→16) remain — those are small, bounded allocations that don't significantly affect fragmentation.

**Alternative:** Keep all buffer reductions. Rejected because with tasks off the critical path, WiFi is the bottleneck — starving it of buffers risks beacon timeout under ENT even after the reorder.

### 5. Dashboard HTTP server remains after WiFi

`httpd_start()` internally allocates a task stack. Under ENT fragmentation, this may still fail (as observed: `ESP_ERR_HTTPD_TASK`). This is acceptable per the existing spec: dashboard is optional, and heap diagnostics will report the failure.

## Risks / Trade-offs

- **WiFi not started on `Init()` callers**: Code that calls `Logger::Init()` and expects WiFi to be up immediately will break. Mitigation: `Logger::Init()` calls both `InitCore()` and `StartWifi()` internally, preserving backward compatibility. Only `app_main` uses the split API.

- **Task start before SNTP sync**: `logger_task` starts before NTP time is acquired. Log timestamps will show epoch 0 until NTP completes. Mitigation: existing code already handles `unix_time=0` gracefully — `CsvLine` records are formatted with epoch 0 and the logger task loop periodically re-syncs time via `BuildTimeInfo()`.

- **WiFi buffer pool may be constrained under ENT**: Dynamic buffers at 16 may starve beacon/probe allocations if ENT auth fragments the remaining heap. Mitigation: reverted dynamic buffers to 32. If ENT still fails, further tuning (e.g., static allocation of WiFi buffers) may be needed.
