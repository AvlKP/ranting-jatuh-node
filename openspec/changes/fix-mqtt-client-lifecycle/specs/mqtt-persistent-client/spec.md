## ADDED Requirements

### Requirement: MQTT client handle persists across publishes

The MQTT client handle SHALL be created once during logger initialization and reused for all subsequent publish operations. It SHALL NOT be destroyed and recreated between publishes.

#### Scenario: Client created at init

- **WHEN** `logger::mqtt::Init()` is called
- **THEN** the MQTT client is initialized via `esp_mqtt_client_init()` and its handle is stored
- **AND** subsequent calls to `Init()` are no-ops for the MQTT client

#### Scenario: Client reused across publishes

- **WHEN** `PublishLines()` or `PublishRawInternal()` is called
- **THEN** the existing MQTT client handle is reused; no new client is created

### Requirement: Publish cycle uses connect-disconnect, not start-stop

After MQTT data is published, the client SHALL issue a clean MQTT DISCONNECT (`esp_mqtt_client_disconnect()`) instead of destroying the client (`esp_mqtt_client_stop()`). The client handle and event handlers SHALL remain registered.

#### Scenario: Publish with disconnect

- **WHEN** `PublishLines()` finishes sending all payload lines
- **THEN** `esp_mqtt_client_disconnect()` is called on the client
- **AND** `esp_mqtt_client_stop()` is NOT called
- **AND** the client handle remains valid for the next publish

### Requirement: TX drain delay before disconnect

The system SHALL wait for in-flight WiFi TX callbacks to complete before disconnecting the MQTT client. A minimum delay of 1000ms SHALL be inserted between the last `esp_mqtt_client_publish()` call and the subsequent `esp_mqtt_client_disconnect()`.

#### Scenario: Delay after publish

- **WHEN** all payload lines have been published via `esp_mqtt_client_publish()`
- **THEN** the task delays for at least 1000ms before calling `esp_mqtt_client_disconnect()`
