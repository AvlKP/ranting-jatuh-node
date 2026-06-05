## 1. StreamSample — State Field

- [x] 1.1 Add `NodeState state` field to `StreamSample` struct in `monitor.hpp`, default `NodeState::IDLE`
- [x] 1.2 In `Monitor::Update()` in `monitor.cpp`, set `sample.state = state_` when building the StreamSample

## 2. CSV Format — State Column

- [x] 2.1 In `ResetDebugLog()` in `logger_storage.cpp`, update CSV header from `timestamp_ms,accel_x,...,tilt_z` to `timestamp_ms,accel_x,...,tilt_z,state`
- [x] 2.2 In `Logger::TaskLoop()` in `logger.cpp`, add `state` column to the `std::snprintf` format string: append `,%u` with `static_cast<unsigned>(event.stream_sample.state)`

## 3. Storage Layer — Ring Buffer

- [x] 3.1 Add ring buffer (64 `CsvLine` entries) and buffer state in anonymous namespace of `logger_storage.cpp`, gated by `CONFIG_APP_DEBUG_CSV_LOGS`
- [x] 3.2 Modify `AppendDebugLog()` to append `CsvLine` into ring buffer instead of calling `AppendLine()` immediately; return false if buffer is full (caller handles early flush)
- [x] 3.3 Add `FlushDebugLog()` — open `debug.csv` once with `fopen("a")`, write all buffered lines in sequence with `fwrite`, close file; reset buffer indices
- [x] 3.4 Declare `FlushDebugLog()` in `logger_internal.hpp` under `namespace logger::storage`

## 4. Task Loop — 1 Hz Flush Trigger

- [x] 4.1 In `Logger::TaskLoop()`, track `last_flush_us` with `esp_timer_get_time()`; after processing StreamSample events, call `FlushDebugLog()` when `NOW - last_flush_us >= 1,000,000` and buffer is non-empty
- [x] 4.2 After `AppendDebugLog()` returns false (buffer full), call `FlushDebugLog()` immediately then retry append; log warning on buffer-full condition

## 5. Shutdown Flush

- [x] 5.1 On logger shutdown or task termination, flush any remaining buffered debug lines before the storage module is deinitialized

## 6. Verification

- [ ] 6.1 Build with `CONFIG_APP_DEBUG_CSV_LOGS=y` and verify no compile errors *(requires Windows PowerShell + ESP-IDF)*
- [ ] 6.2 Build with `CONFIG_APP_DEBUG_CSV_LOGS=n` and verify the feature is fully compiled out (zero code size increase) *(requires Windows PowerShell + ESP-IDF)*
- [ ] 6.3 Run on hardware, verify `debug.csv` receives batched writes and includes `state` column with correct values *(requires ESP32-S3 hardware)*
