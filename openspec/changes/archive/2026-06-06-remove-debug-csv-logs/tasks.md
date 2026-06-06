## 1. Clean Working Tree

- [x] 1.1 Discard all unstaged changes with `git restore .` to baseline at HEAD (027373b)
- [x] 1.2 Verify working tree is clean with `git status`

## 2. Remove Debug CSV from Monitor

- [x] 2.1 Remove `MONITOR_EVENT_STREAM_SAMPLE = 2` from `components/monitor/include/monitor_events.hpp`
- [x] 2.2 Remove `#ifdef CONFIG_APP_DEBUG_CSV_LOGS` stream sample posting block from `components/monitor/monitor.cpp` Update() method

## 3. Remove Debug CSV from Logger

- [x] 3.1 Remove `SetDebugMonitor()` method, `debug_monitor_` member, and `last_flush_us_` member from `components/logger/include/logger.hpp`
- [x] 3.2 Remove `StreamSample = 2U` from EventType enum and `stream_sample` field from Event struct in `components/logger/include/logger.hpp`
- [x] 3.3 Remove `ResetDebugLog()` and `AppendDebugLog()` declarations from `components/logger/include/logger_internal.hpp`
- [x] 3.4 Remove StreamSample event handling branch from `components/logger/logger.cpp` EventHandler
- [x] 3.5 Remove `#if CONFIG_APP_DEBUG_CSV_LOGS` event handler registration for MONITOR_EVENT_STREAM_SAMPLE from `components/logger/logger.cpp` Init()
- [x] 3.6 Remove `storage::ResetDebugLog()` call from `components/logger/logger.cpp` Init()
- [x] 3.7 Remove StreamSample processing and CSV formatting block from `components/logger/logger.cpp` TaskLoop()
- [x] 3.8 Remove `BuildDebugLogPath()`, `ResetDebugLog()`, and `AppendDebugLog()` from `components/logger/logger_storage.cpp`

## 4. Remove Debug CSV from Configuration

- [x] 4.1 Remove "Debug Configuration" menu and `CONFIG_APP_DEBUG_CSV_LOGS` from `main/Kconfig`

## 5. Remove OpenSpec Artifacts

- [x] 5.1 Delete `openspec/specs/debug-csv-logs/` directory
- [x] 5.2 Delete `openspec/changes/archive/2026-06-04-add-debug-csv-logs/` directory
- [x] 5.3 Delete `openspec/changes/archive/2026-06-05-add-debug-csv-logs/` directory

## 6. Verify and Commit

- [x] 6.1 Grep codebase for `DEBUG_CSV`, `StreamSample`, `AppendDebugLog`, `ResetDebugLog`, `SetDebugMonitor`, `debug.csv` to confirm complete removal
- [x] 6.2 Build project to verify compilation succeeds
- [x] 6.3 Commit removal as a single atomic commit
