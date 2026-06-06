## 1. Create MQTT client once at init

- [x] 1.1 Call `EnsureMqttClient()` from `Init()` instead of from every publish
- [x] 1.2 Remove `EnsureMqttClient()` calls from `PublishLines()` and `PublishRawInternal()`
- [x] 1.3 Remove `EnsureMqttClient()` calls from `SyncTimeOnce()` (N/A — already absent)

## 2. Replace stop() with disconnect() in publish paths

- [x] 2.1 Replace `esp_mqtt_client_stop(s_client)` with `esp_mqtt_client_disconnect(s_client)` in `PublishLines()` (both error path and success path)
- [x] 2.2 Replace `esp_mqtt_client_stop(s_client)` with `esp_mqtt_client_disconnect(s_client)` in `PublishRawInternal()` (both error path and success path)
- [x] 2.3 Remove `SyncTime()` and `ConnectWifi()` calls from publish paths (SKIP — still needed for non-dashboard WiFi on/off)

## 3. Add TX drain delay before disconnect

- [x] 3.1 Define drain delay constant (1000ms)
- [x] 3.2 Insert `vTaskDelay(pdMS_TO_TICKS(kTxDrainDelayMs))` after publish loop and before `esp_mqtt_client_disconnect()` in `PublishLines()`
- [x] 3.3 Insert `vTaskDelay(pdMS_TO_TICKS(kTxDrainDelayMs))` after publish and before `esp_mqtt_client_disconnect()` in `PublishRawInternal()`
