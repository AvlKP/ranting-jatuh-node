## Context

The MQTT client in `logger_mqtt.cpp` uses a connect-publish-disconnect cycle: `esp_mqtt_client_start()` → publish → `esp_mqtt_client_stop()` on every publish. The `stop()` call tears down the underlying TCP socket. When this happens while WiFi's ppTask is still processing a TX-done callback from the just-sent publish data, the callback runs on freed/dangling lwIP state, causing either a LoadStoreAlignment panic in `_frxt_int_enter` (ppTask stack corruption) or a `pbuf_free` double-free assertion (lwIP pbuf refcount corruption).

The HTTP server (dashboard) runs on the same lwIP stack concurrently, receiving browser polling requests at 1.5Hz, which compounds the lwIP state churn.

## Goals / Non-Goals

**Goals:**
- Eliminate MQTT-initiated lwIP socket teardown races
- Preserve WiFi-power-off-between-publishes battery profile
- Keep MQTT client handle alive across publishes

**Non-Goals:**
- Change WiFi power management strategy
- Change MQTT publish payloads or topics
- Modify HTTP server or dashboard behavior
- Change MQTT QoS or protocol version

## Decisions

### 1. Use `esp_mqtt_client_disconnect()` instead of `esp_mqtt_client_stop()`

**Rationale**: `disconnect()` sends a clean MQTT DISCONNECT control packet without tearing down the TCP socket. The lwIP socket remains open until the TCP stack drains. After disconnect, `esp_wifi_stop()` (in non-dashboard mode) tears down WiFi cleanly since no lwIP socket is mid-operation. `stop()` destroys the MQTT client handle and calls `esp_transport_close()`, which rips the socket while ppTask may still reference it.

**Alternative considered**: Adding a lock around lwIP operations. Rejected because the WiFi binary blob (libpp.a) runs in ppTask which we can't instrument.

### 2. Create MQTT client once in `Init()`, reuse forever

**Rationale**: `esp_mqtt_client_init()` allocates internal structures and registers event handlers. Creating/destroying this per publish adds unnecessary heap churn and event handler registration overhead. With the handle recreated each time, any in-flight MQTT event handler invocation from a previous cycle could reference freed memory.

**Alternative considered**: `stop()` then `init()` fresh each cycle. Rejected — this IS the current pattern that causes the crash.

### 3. Add 1-second TX drain delay after publish before disconnect

**Rationale**: After `esp_mqtt_client_publish()` returns, the TCP stack may not have finished transmitting the packet. The ppTask TX-done callback fires asynchronously (at priority 23 on Core 0). A 1-second delay gives ppTask time to process all in-flight TX callbacks before we issue `disconnect()`. Without this, `disconnect()` could still race with an in-flight TX-done callback.

## Risks / Trade-offs

- **+1s WiFi-on time per publish**: At ~5-minute publish intervals, this is ~0.3% duty cycle increase. Acceptable.
- **MQTT client handle stays alive**: Occupies a few KB of heap permanently. With 159 KiB heap available, negligible.
- **`esp_mqtt_client_disconnect()` may not exist in all ESP-IDF versions**: Verified in v5.5.4 API.
