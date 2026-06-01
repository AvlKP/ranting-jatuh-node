## Why

The system clock starts at epoch 0 on boot. Time synchronization (SNTP) only happens inside `PublishLines()` and `PublishRawInternal()` — which run periodically when MQTT publishes occur. This means all SD card log entries and monitor timestamps before the first publish cycle have invalid timestamps (epoch ~0), making early data useless for diagnostics and correlation with server-side events.

## What Changes

- Add an initial SNTP time sync step during startup, after WiFi is available but before monitor and logger tasks begin.
- Refactor `SyncTime()` out of the `logger::mqtt` anonymous namespace into a callable internal API so it can be invoked from the initialization sequence.
- Keep the existing periodic time sync in publish paths as a fallback/refresh mechanism.

## Capabilities

### New Capabilities
- `startup-time-sync`: One-shot SNTP time synchronization during boot, ensuring the system clock is valid before any data collection or logging begins.

### Modified Capabilities
_None — no existing spec-level requirements change. The periodic sync behavior is preserved._

## Impact

- **`components/logger/logger_mqtt.cpp`**: `SyncTime()` and potentially `ConnectWifi()` need to be accessible from outside the anonymous namespace.
- **`components/logger/include/logger_internal.hpp`**: New declaration for startup-accessible time sync function.
- **`main/main.cpp`**: New startup step calling time sync after logger MQTT init, before `logger.Start()` and `monitor.Start()`.
- **No breaking changes** — existing periodic sync remains untouched.
