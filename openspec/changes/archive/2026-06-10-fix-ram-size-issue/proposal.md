## Why

Firmware boot can exhaust internal heap after WiFi, dashboard HTTP server, logger queue, and logger task are initialized. In the observed boot log, `logger_task` starts, but `monitor_task` creation fails, so data collection never begins.

## What Changes

- Add a runtime RAM budget requirement for normal boot with dashboard, WiFi, logger, and monitor enabled.
- Reduce or defer RAM-heavy startup allocations so `monitor_task` and `logger_task` both start reliably.
- Add boot-time diagnostics for task creation failures, including free heap and largest allocatable internal block.
- Keep monitor hot paths deterministic and no-heap while preserving existing sampling, SD logging, MQTT, and dashboard behavior.
- Verify memory size with `idf.py size` and runtime boot logs after implementation.

## Capabilities

### New Capabilities
- None.

### Modified Capabilities
- `embedded-runtime-safety`: Add requirements for boot-time heap margin and task creation diagnostics under the normal monitoring build.

## Impact

- Affected code: `main/main.cpp`, `components/monitor`, `components/logger`, `components/dashboard`, and related Kconfig defaults.
- Affected systems: ESP32-S3 internal RAM/heap usage, FreeRTOS task startup, dashboard HTTP server, WiFi/MQTT startup, monitor data collection.
- No external API or payload format changes expected.
