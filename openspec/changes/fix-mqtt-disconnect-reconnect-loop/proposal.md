## Why

`esp_mqtt_client_disconnect()` (MQTT5 mode) triggers an auto-reconnect despite `disable_auto_reconnect = true`, causing `esp_mqtt_client_start()` to fail on subsequent publishes because the client is already started/connecting. This produces infinite "MQTT start failed" loops, blocking all parameter publishing.

## What Changes

- Remove `esp_mqtt_client_disconnect()` calls from publish paths
- Never `disconnect()` or `stop()` the MQTT client between publishes
- Let TCP transport die naturally when WiFi powers off; on next publish, use `esp_mqtt_client_reconnect()` to resume connection
- Track MQTT connection state via event group bits to decide `start()` vs `reconnect()` per publish

## Capabilities

### New Capabilities

- `mqtt-reconnect-on-publish`: MQTT client uses `reconnect()` instead of `start()` for subsequent publishes after initial connection; no explicit disconnect between publishes

### Modified Capabilities

<!-- None -->

## Impact

- `components/logger/logger_mqtt.cpp`: `PublishLines()`, `PublishRawInternal()`
