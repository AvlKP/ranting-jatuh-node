## 1. Remove Stream Sample from esp_event Pipeline

- [x] 1.1 Gate `MONITOR_EVENT_STREAM_SAMPLE` enum value and its `esp_event_post` call in `monitor.cpp:237-241` behind `CONFIG_APP_DEBUG_CSV_LOGS` (remove existing posting; the value stays as unused or is removed)
- [x] 1.2 Gate `MONITOR_EVENT_STREAM_SAMPLE` handler registration in `logger.cpp:250-260` behind `CONFIG_APP_DEBUG_CSV_LOGS` and change to no-op (remove the handler)
- [x] 1.3 Remove `MONITOR_EVENT_STREAM_SAMPLE` case from `Logger::EventHandler` in `logger.cpp:285-287`
- [x] 1.4 Remove `StreamSample EventType::StreamSample` case from `Logger::TaskLoop` in `logger.cpp:346-367`

## 2. Add Dedicated Debug Ring Buffer in Monitor

- [x] 2.1 Add `kDebugRingSize = 256` constant and `std::array<StreamSample, kDebugRingSize>` buffer to `monitor.hpp` (gated by `#if CONFIG_APP_DEBUG_CSV_LOGS`)
- [x] 2.2 Add `std::atomic<std::size_t>` write index and count for lock-free single-producer-single-consumer access
- [x] 2.3 In `Monitor::Update()`, after building the `StreamSample`, write it to the debug ring buffer using atomic indices instead of calling `esp_event_post()`
- [x] 2.4 Expose `GetDebugSamples(StreamSample* out, std::size_t& count, std::size_t max_count)` accessor that atomically drains the ring buffer (reader consumes all available entries)

## 3. Poll Debug Buffer from Logger TaskLoop

- [x] 3.1 In `Logger::TaskLoop`, during the 1-second flush check, call `monitor.GetDebugSamples()` to drain the ring buffer
- [x] 3.2 Format each drained `StreamSample` as a `CsvLine` and call `storage::AppendDebugLog()`
- [x] 3.3 Pass a reference to the monitor's debug buffer to the logger (add `SetDebugMonitor(Monitor&)` accessor)
- [x] 3.4 Ensure debug CSV flush timing remains at 1 Hz (no change to the existing `FlushDebugLog` logic)

## 4. Add SD Card Health Detection

- [x] 4.1 Add `std::atomic<bool> g_sd_healthy{true}` and `g_sd_unhealthy_since_us{0}` in `logger_storage.cpp`
- [x] 4.2 Add `storage::MarkSdUnhealthy()` to set the flag and timestamp
- [x] 4.3 Add `storage::IsSdHealthy()` that returns false if unhealthy and cooldown (5 seconds) not elapsed; resets to healthy after cooldown
- [x] 4.4 Call `MarkSdUnhealthy()` at every SD I/O failure point: `AppendLine()`, `EnsureFileHeader()`, `FlushDebugLog()`, `ResetDebugLog()`
- [x] 4.5 Guard `AppendParameter()`, `AppendFailure()`, `FlushDebugLog()`, `AppendDebugLog()` with `IsSdHealthy()` check — skip and return false if unhealthy

## 5. Add SD Health Guard to Dashboard StatusHandler

- [x] 5.1 In `Dashboard::StatusHandler`, before `opendir()`, check `storage::IsSdHealthy()`; skip directory listing if unhealthy (return empty `"files":[]`)
- [x] 5.2 In `Dashboard::DownloadHandler`, before `fopen()`, check `storage::IsSdHealthy()`; return 503 Service Unavailable if unhealthy
- [x] 5.3 Expose `storage::IsSdHealthy()` and `storage::MarkSdUnhealthy()` in `logger_internal.hpp`

## 6. Integration and Verification

- [ ] 6.1 Build with `CONFIG_APP_DEBUG_CSV_LOGS=n` and verify dashboard loads, stream samples populate, state transitions work, no SD card crashes on repeated polling
- [ ] 6.2 Build with `CONFIG_APP_DEBUG_CSV_LOGS=y` and verify: no "event queue full" warnings, logger receives debug samples at 1 Hz, debug.csv file is written in batches, dashboard remains responsive
- [ ] 6.3 Verify SD health cooldown by unplugging SD card mid-operation: dashboard continues serving, system does not reset, health flag recovers after reinsert + cooldown
