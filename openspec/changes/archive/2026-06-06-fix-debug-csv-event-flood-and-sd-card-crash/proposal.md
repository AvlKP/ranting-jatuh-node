## Why

The `add-debug-csv-logs` and `mqtt-node-topic-prefix` changes introduced two critical regressions: (1) enabling debug CSV floods the esp_event loop with 208 events/sec, silently dropping MONITOR_EVENT_RESULT events and starving the HTTP server; (2) SD card timeout errors in the dashboard StatusHandler cause the FATFS driver to block indefinitely, triggering a task watchdog reset that reboots the system.

## What Changes

- **Decouple debug CSV from esp_event pipeline**: Replace `esp_event_post(STREAM_SAMPLE)` at IMU rate with a dedicated ring buffer in the monitor component. The logger polls/flushes at its existing 1 Hz interval. No event posting per sample.
- **Add SD card operation resilience**: Wrap `opendir()`, `stat()`, and `fopen()` in the dashboard StatusHandler with bounded-time retry logic. Detect repeated SDMMC timeouts and skip SD-dependent sections gracefully rather than blocking the HTTP handler task.
- **Add SD health guard in logger**: Before attempting SD writes (`AppendParameter`, `AppendFailure`, `FlushDebugLog`), check a health flag to skip operations when the card is unresponsive, avoiding queued-up timeout cycles.
- **Remove `MONITOR_EVENT_STREAM_SAMPLE` from the esp_event system**: No longer registered as an event handler in logger. Compile-time gated by `CONFIG_APP_DEBUG_CSV_LOGS`.

## Capabilities

### New Capabilities
- `sd-card-resilience`: Wrap SD card operations in the dashboard and logger with bounded-time retry and graceful degradation when the card is unresponsive.

### Modified Capabilities
- `debug-csv-logs`: Stream sample data delivery changes from `esp_event_post` per-sample to a dedicated ring buffer polled by the logger at 1 Hz. The batch-flush requirement is preserved; the delivery mechanism changes.

## Impact

- **monitor.cpp**: Add dedicated ring buffer for stream samples; remove per-sample `esp_event_post` call; expose a `GetDebugSamples()` accessor.
- **dashboard.cpp**: Wrap `opendir()`, `stat()` calls in StatusHandler with timeout/bounded-retry; skip SD sections on repeated failure.
- **logger.cpp**: Remove `MONITOR_EVENT_STREAM_SAMPLE` event handler registration; poll monitor's debug buffer in TaskLoop instead.
- **logger_storage.cpp**: Add `IsSdHealthy()` check before `AppendParameter`, `AppendFailure`, `FlushDebugLog`; skip writes when unhealthy.
- **monitor_events.hpp**: Remove `MONITOR_EVENT_STREAM_SAMPLE` enum value (compile-time gated).
- **logger_internal.hpp**: Update `storage::AppendDebugLog` signature if needed; expose `storage::IsSdHealthy()`.
