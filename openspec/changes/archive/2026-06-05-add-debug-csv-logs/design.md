## Context

The debug CSV logging system in `logger::storage` writes each IMU stream sample immediately to the SD card via `fopen`/`fwrite`/`fclose`. At typical IMU sample rates (52–104 Hz), this produces 50–100 file open/close cycles per second on a FAT filesystem over SDIO 1-bit, consuming significant time in the logger task. The original design document for debug CSV logs explicitly identified this risk and recommended buffering, but it was never implemented.

The logger task (`TaskLoop`, priority 4 on core 0) is the sole consumer of `StreamSample` events. It currently formats each sample into a `CsvLine` and calls `storage::AppendDebugLog()` immediately. All debug log writes happen from this single thread; no concurrency concerns exist.

## Goals / Non-Goals

**Goals:**
- Reduce SD card I/O overhead for debug CSV logging by batching writes
- Flush accumulated debug log lines to SD card at ~1 Hz (once per second)
- Keep `kCsvLineMax = 512` bytes per entry, buffer up to 64 entries (~32 KB static allocation)

**Non-Goals:**
- Do not modify the parameters calculation pipeline (`monitor.cpp`, parameter computation paths)
- Do not change the CSV format, file path, Kconfig gating, or header
- Do not buffer parameter CSV or failure CSV writes (they occur at much lower frequency)

## Decisions

**Decision 1: Buffer in `logger_storage.cpp`, flush triggered from `TaskLoop`**

The ring buffer lives inside `logger::storage` (anonymous namespace in `logger_storage.cpp`). `AppendDebugLog()` appends to the buffer instead of writing immediately. A new `FlushDebugLog()` function writes all buffered lines to the file in a single `fopen`/batch-`fwrite`/`fclose` cycle. The `TaskLoop` in `logger.cpp` tracks elapsed time with `esp_timer_get_time()` and calls `FlushDebugLog()` every 1 second.

*Rejected alternative: buffer in `TaskLoop` directly.* This would avoid changing the storage API but mixes I/O buffering logic into the task loop, making it harder to test and reuse.

**Decision 2: Ring buffer of 64 `CsvLine` entries, flush-on-full fallback**

At the max IMU rate (208 Hz), 64 entries last ~308 ms. At 104 Hz, ~615 ms. At 52 Hz, 1.23 seconds. Configuring 64 entries balances memory (64 × 512 = 32 KB) against flush frequency. If the buffer fills before the 1-second timer elapses, an early flush is triggered to prevent data loss.

*Rejected alternative: dynamic allocation or larger buffer.* 32 KB is acceptable static overhead for this project. Dynamic allocation adds fragmentation risk and is discouraged by the real-time C++ guidelines.

**Decision 3: Single `fopen("a")` + looped `fwrite` + single `fclose` per flush**

On flush, open the file once in append mode, write all buffered lines sequentially with `fwrite`, then close. This reduces the file-open overhead from O(N) to O(1) per batch.

**Decision 4: Add `NodeState state` field to `StreamSample`**

The `StreamSample` struct gets a `NodeState state` field (default `NodeState::IDLE`). In `Monitor::Update()`, `PushSample()` runs before the `StreamSample` is built, so `state_` already reflects the latest FSM transition. The sample is populated with `sample.state = state_` alongside existing fields. The logger formats this as a `state` column (0 or 1) in the CSV row.

*Rejected alternative: tracking state in the logger via MonitorResult events.* This would introduce ordering complexity since MonitorResult events fire at lower frequency than stream samples. Embedding state in each StreamSample keeps the data self-contained.

**Decision 5: Update CSV header and format string**

CSV header extended from `timestamp_ms,accel_x,accel_y,accel_z,tilt_x,tilt_y,tilt_z` to `timestamp_ms,accel_x,accel_y,accel_z,tilt_x,tilt_y,tilt_z,state`. Format string adds `%u` for state cast to `unsigned int`. Value: `0` for IDLE, `1` for DISTURBED.

**Decision 6: Keep `ResetDebugLog()` unchanged aside from header**

The reset (truncate + header write) happens once at startup and is not performance-sensitive. It remains unbuffered.

## Risks / Trade-offs

| Risk | Mitigation |
|---|---|
| Data loss on unexpected reset: 1 second of buffered samples lost if power fails before flush | Acceptable. Debug logs are non-critical; the 1-second window is small. |
| Buffer overflow at very high IMU rates (208 Hz) causes early flushes, defeating batching | Early flush is still better than per-sample writes. 64 entries at 208 Hz = ~308 ms between flushes—still ~64× fewer `fopen` calls than unbuffered. |
| 32 KB static memory overhead | Acceptable for ESP32-S3 with 512 KB SRAM. Compile-time guarded by `CONFIG_APP_DEBUG_CSV_LOGS`. |
| Flush call in task loop adds ~1 ms latency every second | Negligible vs. the cumulative savings from avoiding per-sample `fopen`/`fclose`. |
