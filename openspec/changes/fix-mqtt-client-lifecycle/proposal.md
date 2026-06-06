## Why

The MQTT client performs a full connect-publish-disconnect cycle on every publish (calling `esp_mqtt_client_stop()` after each publish), which tears down the TCP socket while WiFi's ppTask may still be processing in-flight TX-done callbacks. This causes a LoadStoreAlignment panic in `_frxt_int_enter` inside ppTask and a `pbuf_free` double-free assertion in lwIP's HTTP recv path. The debug CSV feature exposed this latent race by shifting Core 0 scheduling timing with increased SD I/O, but the root cause is the MQTT client lifecycle pattern.

## What Changes

- Create MQTT client once at init, keep the handle alive for the lifetime of the node
- Replace `esp_mqtt_client_stop()` with `esp_mqtt_client_disconnect()` on each publish (clean MQTT DISCONNECT, no socket teardown)
- Add a TX drain delay after publish before disconnecting, allowing ppTask to finish in-flight callbacks
- Remove per-publish MQTT client creation/destruction from publish paths

## Capabilities

### New Capabilities

- `mqtt-persistent-client`: MQTT client handle created once at init, reused across publishes via connect/disconnect cycle instead of start/stop

### Modified Capabilities

<!-- None - existing behavior (publishing parameters/failures) unchanged -->

## Impact

- `components/logger/logger_mqtt.cpp`: `EnsureMqttClient()`, `PublishLines()`, `PublishRawInternal()`, `Init()`
- `components/logger/logger.cpp`: `Init()` now calls MQTT init once
- Battery impact: +1s WiFi-on time per publish (~0.3% duty cycle increase at 5-min interval)
