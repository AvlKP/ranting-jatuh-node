## 1. Static Runtime-Safety Audit

- [x] 1.1 Review task creation, stack sizes, priorities, core pinning, and task handles in `main`, `monitor`, `logger`, `dashboard`, WiFi, MQTT, and ESP event usage
- [x] 1.2 Scan monitor/logger/dashboard/filter code for large automatic buffers, large by-value objects, recursion, unbounded loops, hot-path heap allocation, and unchecked `snprintf`/path/topic formatting
- [x] 1.3 Audit ISR and ESP event handler paths for blocking calls, mutex waits, file/network I/O, and non-ISR-safe operations
- [x] 1.4 Audit Kconfig-derived sizes (`kStorageSamples`, FFT buffers, short buffers, queue depth, dashboard query limits, MQTT log buffers) and list configurations that can exceed RAM or stack limits
- [x] 1.5 Record confirmed findings with file/function, failure mode, reproduction path, and proposed fix

## 2. Stack and RAM Instrumentation

- [x] 2.1 Extend verification logging to report high-water marks for available named application tasks
- [x] 2.2 Add or reuse diagnostics for HTTP server, ESP event, ESP timer, WiFi, and MQTT tasks where FreeRTOS task handles or task snapshots are available
- [x] 2.3 Run `idf.py size` before fixes and record `.bss`, heap, and flash impact baseline
- [x] 2.4 Enable or document ESP-IDF stack guards used for validation (`CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY`, compiler stack check mode, panic output)

## 3. Fix Confirmed Defects

- [x] 3.1 Move confirmed large recurring automatic buffers to bounded persistent storage or chunked streaming when measured stack margin is insufficient
- [x] 3.2 Add `static_assert` checks for Kconfig-derived buffer sizes that affect static RAM, stack, queue item size, or event payload size
- [x] 3.3 Fix unchecked serialization/path/topic formatting paths that can emit invalid JSON/CSV, truncate silently, or overflow fixed buffers
- [x] 3.4 Fix ISR/task communication bugs found in acoustic emission event handling, including missed events or unsafe shared-state access
- [x] 3.5 Expose or log queue/event backpressure counters for dropped logger events and failed monitor event posts
- [x] 3.6 Keep monitor sampling, ESP event handlers, and ISR paths free of newly introduced dynamic allocation or blocking I/O

## 4. Tests and Verification

- [x] 4.1 Add focused Unity tests or host-build tests for fixed buffer-bound and formatting edge cases
- [x] 4.2 Run monitor component tests, including modal and algorithm tests
- [x] 4.3 Build firmware with ESP-IDF environment using `idf.py build`
- [x] 4.4 Run `idf.py size` after fixes and compare RAM/flash changes against baseline
- [x] 4.5 On ESP32-S3 hardware, exercise idle sampling, DISTURBED entry/exit, post-hoc modal analysis, dashboard `/api/status`, file download, SD writes, MQTT publish, and acoustic emission/free-fall events
- [x] 4.6 Record final stack high-water marks and confirm no stack canary panic, watchdog reset, FreeRTOS assert, event queue overload, or malformed dashboard/logger output occurs
