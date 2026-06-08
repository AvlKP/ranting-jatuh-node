## Why

Recent monitor changes moved heavy modal-analysis state out of the 8 KiB monitor task stack, but firmware still has several embedded runtime risk areas that are easy to miss: large automatic buffers in HTTP handlers, long compute paths on periodic tasks, mutex/event interactions, queue backpressure, and stack checks that are not tied to acceptance.

This change adds a focused ESP-IDF runtime-safety audit and fixes confirmed implementation bugs that can cause stack overflow, watchdog stalls, dropped events, corrupted JSON/CSV output, or nondeterministic behavior on ESP32-S3.

## What Changes

- Audit all firmware tasks, ESP event handlers, HTTP handlers, GPIO ISR paths, and logger/MQTT/storage paths for stack, heap, blocking, buffer-bound, and concurrency hazards.
- Measure stack high-water marks for `main`, `monitor_task`, `logger_task`, HTTP server task, ESP event task, timer task, and relevant WiFi/MQTT tasks where handles are available.
- Replace confirmed large automatic buffers or hot-path dynamic allocation with bounded static/member storage, chunked streaming, or explicitly sized task stack only when measurement justifies it.
- Add compile-time and runtime guardrails for buffer sizes derived from Kconfig, especially monitor storage windows, FFT scratch, modal analysis scratch, dashboard query buffers, logger queue items, and MQTT log buffers.
- Add focused Unity tests or host-build checks for boundary cases found during audit.
- No public MQTT, dashboard, or monitor API behavior changes unless required to fix a confirmed defect.

## Capabilities

### New Capabilities
- `embedded-runtime-safety`: Firmware runtime safety requirements for bounded stack use, deterministic hot paths, ISR/event-handler safety, buffer bounds, and ESP-IDF diagnostics.

### Modified Capabilities
None. This change audits and fixes implementation-level defects without changing user-visible monitoring behavior.

## Impact

- Affected code: `main/`, `components/monitor/`, `components/logger/`, `components/dashboard/`, `components/filter/`, and related Unity tests.
- Affected configuration: ESP-IDF stack checking, task stack sizes, monitor/logger/dashboard Kconfig-derived buffer limits, and optional verification logging.
- Build/runtime verification: `idf.py build`, `idf.py size`, Unity monitor tests, stack high-water logging, and serial monitor validation on ESP32-S3 hardware.
- No dependency or protocol changes expected.
