## Why

On-demand WiFi currently saves power by stopping the radio after each publish, but the reconnect path is unreliable on WPA2-Enterprise networks. Runtime log `build/log/idf_py_stdout_output_2956` shows first WiFi+MQTT publish succeeds, then the next on-demand reconnect reaches association but times out before the ESP-IDF WPA handshake timer reports `reason=204 (handshake_timeout)`.

## What Changes

- Harden the on-demand WiFi lifecycle so every failed connect attempt leaves the radio and event state clean for the next attempt.
- Align the on-demand connect wait with ESP-IDF Enterprise handshake timing instead of timing out early at 20 seconds.
- Treat intentional on-demand release disconnects (`assoc_leave` / `auth_leave`) as expected cleanup, not network failure.
- Separate transient wait bits from durable connection state so `IsConnected()` does not consume its own state.
- Add diagnostics that preserve the real disconnect reason when ESP-IDF reports it after the application-level wait ends.
- Keep the existing field-mode power policy: on-demand still starts WiFi only for eligible publish cycles and stops it after release.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `network-strategy`: strengthen on-demand WiFi connect, release, retry, timeout, and diagnostics requirements.

## Impact

- Affected code: `components/logger/network_on_demand.cpp`, `components/logger/include/network_strategy.hpp`, and targeted call sites in `components/logger/network_task.cpp` if cleanup or diagnostics need API support.
- Affected systems: ESP32-S3 WiFi station lifecycle, WPA2-Enterprise reconnect behavior, network-task backoff, MQTT publish availability, and field-mode power usage.
- No MQTT topic, payload schema, storage format, or logger event API changes.
