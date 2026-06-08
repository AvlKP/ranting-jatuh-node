## Static Audit Findings

### Confirmed Defects Fixed

- `components/monitor/monitor.cpp::ComputeAndPublish`
  - Failure mode: `CONFIG_MONITOR_DEBUG_DUMP=y` build failed because debug logging referenced removed local names `roll_modal` and `pitch_modal`.
  - Reproduction path: `idf.py size` rebuilt monitor with current config and stopped at undefined identifiers.
  - Fix: Use persistent `roll_modal_scratch_` and `pitch_modal_scratch_` members in debug log.

- `components/dashboard/dashboard.cpp::StatusHandler`
  - Failure mode: recurring HTTP status path allocated stream samples, history buffers, FFT buffer, MQTT logs, path buffer, and JSON chunk buffer on HTTP server stack. Worst local scratch was about 4.5 KiB before deeper VFS/HTTP call frames.
  - Reproduction path: repeated `/api/status` with FFT/log/file data while dashboard server uses a 12 KiB stack.
  - Fix: Move recurring status scratch buffers to bounded static storage and check each formatted chunk before sending.

- `components/dashboard/dashboard.cpp::StatusHandler`
  - Failure mode: SD filenames and MQTT log lines were inserted into JSON without complete bounds/escape handling. Long filenames could truncate silently; quotes/backslashes could corrupt JSON.
  - Reproduction path: long regular SD filename or MQTT log with quotes/backslashes requested through `/api/status`.
  - Fix: Check path/format return values; skip unsafe entries; escape JSON strings within fixed buffers.

- `components/monitor/monitor.cpp::AeGpioIsr` and `CheckFailureEvents`
  - Failure mode: GPIO ISR wrote a `volatile bool`, so multiple AE edges before the next monitor sample collapsed into one event. Shared state also relied on volatile rather than a FreeRTOS critical section.
  - Reproduction path: two or more AE pulses within one monitor sample period.
  - Fix: Count pending AE events in an ISR critical section, drain count in task context.

- `components/monitor/monitor.cpp::ComputeAndPublish` and `PublishFailure`
  - Failure mode: failed `esp_event_post(..., 0)` calls were ignored, hiding event-loop backpressure.
  - Reproduction path: full ESP event queue or event-loop failure during monitor result/failure post.
  - Fix: Count and log dropped result/failure posts.

### Runtime/Configuration Risk Notes

- Task creation:
  - `monitor_task`: 8192 bytes, priority 5, core 1, task handle now retained.
  - `logger_task`: 6144 bytes, priority 4, core 0, task handle retained.
  - Dashboard HTTP server: 12288 bytes, ESP-IDF HTTP server task, handler stack pressure reduced.
  - ESP event/timer tasks: configured by ESP-IDF; current stack sizes are logged by verification.

- Kconfig-derived memory:
  - Current `CONFIG_MONITOR_STORAGE_MINUTES=1`, `CONFIG_MONITOR_IMU_RATE_HZ=52`, so `kStorageSamples=3120`.
  - Kconfig still allows much larger values; compile-time guards now reject storage windows above 32768 samples, short buffers above 1024 samples, and invalid DISTURBED refresh margin.
  - Current stack guards: FreeRTOS stack canary enabled; compiler stack check mode disabled. Verification logs both states.

- Remaining hardware validation:
  - Final stack high-water marks require ESP32-S3 runtime exercise of monitor, logger, dashboard, SD, MQTT, AE/free-fall paths.
  - This session did not flash or run on hardware.
