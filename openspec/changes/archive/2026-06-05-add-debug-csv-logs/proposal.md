## Why

Debug CSV logging performs an `fopen`/`fwrite`/`fclose` cycle on the SD card for every IMU sample (26–208 Hz). This unbuffered I/O pattern consumes significant time in the logger task, degrading overall system responsiveness. The original design acknowledged this risk and recommended buffering, but it was never implemented.

## What Changes

- Add a ring buffer to accumulate debug CSV lines in memory before writing
- Flush the buffer to the SD card once per second (1 Hz periodic flush)
- Add `state` column to debug CSV (0=IDLE, 1=DISTURBED) on every row so state transitions are visible
- Add `NodeState state` field to `StreamSample` struct populated by monitor
- Do not modify the parameters calculation pipeline or any other subsystem

## Capabilities

### New Capabilities
<!-- None -->

### Modified Capabilities
- `debug-csv-logs`: Debug CSV lines SHALL be buffered and flushed to SD card at 1 Hz instead of written immediately per sample. CSV format extended with `state` column. File location and Kconfig gating remain unchanged.

## Impact

- `components/monitor/include/monitor.hpp`: Add `state` field to `StreamSample`
- `components/monitor/monitor.cpp`: Populate `StreamSample::state` from `state_`
- `components/logger/logger_storage.cpp`: Add ring buffer, periodic flush, update CSV header with `state` column
- `components/logger/logger.cpp`: Task loop integration for 1 Hz flush, add `state` column to CSV format string
