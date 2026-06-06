## ADDED Requirements

### Requirement: First publish uses start, subsequent publishes use reconnect

The system SHALL use `esp_mqtt_client_start()` on the first publish after client initialization and `esp_mqtt_client_reconnect()` on all subsequent publishes. The system SHALL NOT call `esp_mqtt_client_disconnect()` or `esp_mqtt_client_stop()` between publishes.

#### Scenario: First publish after init

- **WHEN** a publish is requested and the MQTT client has never successfully connected
- **THEN** `esp_mqtt_client_start()` is called to initiate the connection

#### Scenario: Subsequent publish after prior success

- **WHEN** a publish is requested and the MQTT client has previously connected successfully
- **THEN** `esp_mqtt_client_reconnect()` is called instead of `esp_mqtt_client_start()`

#### Scenario: No explicit disconnect between publishes

- **WHEN** all payload data has been published
- **THEN** no `esp_mqtt_client_disconnect()` or `esp_mqtt_client_stop()` call is made
- **AND** the MQTT client remains in its current transport state
