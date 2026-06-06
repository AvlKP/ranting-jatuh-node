## 1. Remove disconnect calls from publish paths

- [x] 1.1 Remove `esp_mqtt_client_disconnect(s_client)` from `PublishLines()` (both error and success paths)
- [x] 1.2 Remove `esp_mqtt_client_disconnect(s_client)` from `PublishRawInternal()` (all paths)

## 2. Track first-successful-connect state

- [x] 2.1 Add `s_mqtt_ever_connected` static bool in anonymous namespace, initialized false
- [x] 2.2 Set `s_mqtt_ever_connected = true` in `MqttEventHandler()` when `MQTT_EVENT_CONNECTED` received

## 3. Use start vs reconnect in publish paths

- [x] 3.1 In `PublishLines()`: if `s_mqtt_ever_connected`, use `esp_mqtt_client_reconnect()`, else use `esp_mqtt_client_start()`
- [x] 3.2 In `PublishRawInternal()`: same pattern as 3.1
- [x] 3.3 Handle `reconnect()` failure: if it returns error, fall back to `esp_mqtt_client_start()`
