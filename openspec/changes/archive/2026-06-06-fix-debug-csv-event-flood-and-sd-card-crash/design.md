## Context

The `add-debug-csv-logs` change introduced `MONITOR_EVENT_STREAM_SAMPLE` events posted via `esp_event_post()` at the IMU sample rate (up to 208 Hz). The esp_event loop has a limited internal queue (~32 entries default), and the event loop task runs at lower priority than the monitor task. At 208 Hz, the event loop queue fills in ~154ms, after which all event types (including `MONITOR_EVENT_RESULT` and `MONITOR_EVENT_FAILURE`) are silently dropped. The return value of `esp_event_post()` is unchecked at all four call sites.

Additionally, the dashboard StatusHandler calls `opendir()` and `stat()` on the SD card. When the SD card becomes unresponsive (SDMMC timeout error 0x107), the FATFS driver retries reads internally, blocking the HTTP handler task for seconds. The cumulative delay exceeds the task watchdog threshold, triggering a system reset.

## Goals / Non-Goals

**Goals:**
- Eliminate esp_event queue flooding by removing per-sample `esp_event_post()` for stream samples
- Deliver stream sample data to the logger via a lock-free ring buffer, polled at 1 Hz
- Prevent SD card timeouts in the dashboard StatusHandler from blocking indefinitely
- Detect SD card unhealthiness in the logger and skip I/O operations gracefully
- Preserve all existing CSV format, batch-flush timing, and state column behavior

**Non-Goals:**
- Do not change the IMU sample rate or monitor processing pipeline
- Do not modify MQTT publishing logic or topic construction
- Do not change the dashboard's HTML/JS frontend
- Do not fix the underlying SDMMC hardware timeout (hardware-level concern)

## Decisions

**Decision 1: Dedicated ring buffer in monitor, polled by logger**

Replace `esp_event_post(MONITOR_EVENT_STREAM_SAMPLE)` with a lock-free circular buffer inside the monitor component. The monitor writes `StreamSample` entries at IMU rate with atomic indices. The logger's `TaskLoop` polls this buffer at its 1 Hz flush interval, formats CSV lines, and appends to the storage ring buffer.

*Rejected alternative: Increase esp_event queue size.* The esp_event system is designed for control events (state changes, failures), not high-frequency sensor data. Increasing the queue size delays rather than solves the problem; at 208 Hz, any finite queue eventually overflows if consumers can't keep up.

*Rejected alternative: Downsample in monitor before posting.* This loses data and defeats the purpose of debug CSV logging (capturing raw sensor data for offline analysis).

**Decision 2: Single ring buffer, atomic indices, no mutex**

One writer (monitor task, core 1) and one reader (logger task, core 0). Use `std::atomic<std::size_t>` for write index and count. The buffer holds a fixed number of `StreamSample` entries sized for the maximum expected accumulation between logger polls (208 Hz × 1.1s safety margin ≈ 230 entries, rounded to 256).

*Rejected alternative: Mutex-protected buffer in storage namespace.* The mutex in `GetLatestSamples()` already causes minor contention; adding another lock for debug CSV would compound this. Atomic indices are simpler and proven for single-producer-single-consumer patterns.

**Decision 3: SD card health flag with cooldown**

A `std::atomic<bool>` health flag, shared between logger storage and dashboard. On any SDMMC error (`fopen`/`fwrite`/`opendir`/`stat` failure with errno 5 or 107), set the flag to unhealthy. After a cooldown period (5 seconds), reset to healthy. During unhealthy periods:
- Dashboard StatusHandler skips `opendir()` and file-stat sections (returns empty arrays)
- Logger storage skips `AppendParameter`, `AppendFailure`, `FlushDebugLog` (returns false gracefully)
- Logger task does NOT block on SD I/O; it continues processing other events

*Rejected alternative: Per-operation retry with short timeout.* FATFS/SDMMC retry logic is in the driver layer; wrapping with `alarm()` or task-level timeouts adds significant complexity for little benefit. A cooldown flag is simpler and prevents cascading failures.

**Decision 4: Remove `MONITOR_EVENT_STREAM_SAMPLE` enum value when debug CSV disabled**

The event ID `MONITOR_EVENT_STREAM_SAMPLE` is compile-time gated by `CONFIG_APP_DEBUG_CSV_LOGS`. When disabled, the enum value and the logger's event handler registration are compiled out. This prevents any accidental overhead from the event type existing.

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| 256-entry ring buffer (~14 KB) adds static RAM overhead | Acceptable for ESP32-S3 (512 KB SRAM). Compile-time gated by `CONFIG_APP_DEBUG_CSV_LOGS`. |
| 1-second polling means up to 208 samples lost on power failure (vs. previous per-sample queue which also dropped) | Acceptable. Debug logs are non-critical. The previous approach lost samples via queue drops anyway. |
| SD health cooldown of 5 seconds means dashboard shows empty file list during unhealthy periods | Acceptable trade-off vs. system crash. The file list is cosmetic. |
| Atomic ring buffer requires careful ordering on multi-core | ESP32-S3 is single-bus, so `std::atomic` with default `memory_order_seq_cst` is sufficient. Single-producer-single-consumer pattern is well-tested. |
| Drain buffer stack overflow: 256-entry `StreamSample` array (~12 KB) on logger task stack (6 KB) caused `vApplicationStackOverflowHook` | Reduced `kDebugDrainMax` to 32 entries (~1.5 KB). Loop `do { drain 32 } while (drain_count == 32)` to drain the full ring buffer in 1-8 iterations instead of one shot. |
