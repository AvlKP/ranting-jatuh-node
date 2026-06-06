## Context

`fix-mqtt-client-lifecycle` replaced `esp_mqtt_client_stop()` with `esp_mqtt_client_disconnect()` to avoid TCP socket teardown races during ppTask TX-done callbacks. However, `esp_mqtt_client_disconnect()` in MQTT5 mode unconditionally schedules an auto-reconnect (the log shows "Reconnect after 10000 ms"), ignoring `disable_auto_reconnect = true`. On the next publish cycle, `esp_mqtt_client_start()` fails because the client is already connecting or connected.

The dashboard mode keeps WiFi persistently on between publishes (via `CONFIG_DASHBOARD_ENABLE`), so the transport layer stays alive. In non-dashboard mode, WiFi powers off between publishes, which kills the TCP connection naturally.

## Goals / Non-Goals

**Goals:**
- Eliminate reconnect loop from `disconnect()` auto-reconnect
- Preserve TCP socket teardown safety (no ppTask race)
- Preserve WiFi-power-off battery profile
- Handle both first-time publish and subsequent publishes correctly

**Non-Goals:**
- Change WiFi power management
- Change MQTT protocol version or QoS

## Decisions

### 1. Never call `disconnect()` or `stop()` between publishes

Let WiFi on/off manage transport lifecycle. When WiFi disassociates, the TCP connection dies and the MQTT transport layer notices. The MQTT client handle stays alive, event handlers stay registered, only the transport goes down.

### 2. Track whether client has ever successfully connected

Use a static bool `s_mqtt_ever_connected` set by `MQTT_EVENT_CONNECTED`. First publish: `esp_mqtt_client_start()`. Subsequent publishes: `esp_mqtt_client_reconnect()`.

### 3. Wait for connect event after start/reconnect

The 1-second TX drain delay stays. But before publishing, we still wait for `kMqttConnectedBit` via event group. Start vs reconnect both produce `MQTT_EVENT_CONNECTED`.

## Risks / Trade-offs

- `esp_mqtt_client_reconnect()` may fail if WiFi hasn't fully come up → retry with `start()` as fallback
- In dashboard mode (WiFi always on), MQTT client stays connected even between publishes → negligible extra power (MQTT client idle on existing WiFi connection)
