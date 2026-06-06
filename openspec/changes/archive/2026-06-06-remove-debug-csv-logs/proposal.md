## Why

The debug CSV logging feature (introduced in `add-debug-csv-logs`) couples the monitor and logger through an `esp_event` stream for every IMU sample (26–208 Hz), adds a `StreamSample` struct, and requires buffered SD card I/O with 1 Hz flush logic. This approach led to a bug that proved unresolvable within the current architecture. The feature is being removed entirely to restore a clean baseline.

## What Changes

- Remove `StreamSample` event type from monitor and logger event system
- Remove `CONFIG_APP_DEBUG_CSV_LOGS` Kconfig option and all `#ifdef`-gated debug logging code
- Remove `ResetDebugLog` and `AppendDebugLog` from logger storage
- Remove `SetDebugMonitor` method from logger public API
- Remove `debug.csv` file path construction and write logic from logger storage
- Remove debug CSV ring buffer and 1 Hz flush timer from logger task loop
- Remove `MONITOR_EVENT_STREAM_SAMPLE` event from monitor events
- Remove `debug-csv-logs` spec from openspec/specs
- Remove `add-debug-csv-logs` archive artifacts from openspec/changes/archive

## Capabilities

### New Capabilities

<!-- None -->

### Modified Capabilities

- `debug-csv-logs`: Removed entirely. CSV debug logging capability is deleted from the system. No debug CSV files are written, no StreamSample events are posted, no debug CSV configuration exists.

## Impact

- `components/logger/`: Remove StreamSample event handling, debug log storage functions, ring buffer, 1 Hz flush, SetDebugMonitor, Kconfig debug entries
- `components/monitor/`: Remove StreamSample event posting in Update(), remove MONITOR_EVENT_STREAM_SAMPLE
- `main/Kconfig`: Remove Debug Configuration menu and CONFIG_APP_DEBUG_CSV_LOGS
- `main/main.cpp`: Remove SetDebugMonitor call
- `openspec/specs/debug-csv-logs/`: Delete spec directory
- `openspec/changes/archive/2026-06-04-add-debug-csv-logs/`: Delete archive directory
- `openspec/changes/archive/2026-06-05-add-debug-csv-logs/`: Delete archive directory
