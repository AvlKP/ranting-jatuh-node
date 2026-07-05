## MODIFIED Requirements

### Requirement: Network task implements exponential backoff
The network task SHALL implement exponential backoff on connection failure to bound reconnect load. In on-demand mode the backoff applies to WiFi or MQTT connection failures. In persistent mode the backoff applies to MQTT connection failures only, because WiFi reconnect backoff is owned by the persistent network strategy.

#### Scenario: Initial backoff on first failure
- **WHEN** a connection attempt fails for the first time
- **THEN** the network task SHALL wait at least 60 seconds before the next attempt

#### Scenario: Doubling backoff on repeated failures
- **WHEN** consecutive connection attempts fail
- **THEN** the backoff interval SHALL double after each failure, up to a maximum of 1 hour

#### Scenario: Backoff reset on success
- **WHEN** a connection attempt succeeds after previous failures
- **THEN** the backoff interval SHALL reset to 60 seconds

#### Scenario: No publish attempts during backoff
- **WHEN** the backoff timer has not expired
- **THEN** the network task SHALL NOT attempt any WiFi or MQTT connection

#### Scenario: Persistent mode does not double-count WiFi reconnect
- **WHEN** the persistent strategy is retrying WiFi reconnect on its own backoff schedule
- **THEN** the network task SHALL NOT also apply its own backoff to the same WiFi disconnect
- **AND** the network task backoff SHALL apply only when an MQTT connect attempt fails

### Requirement: Network task manages MQTT client lifecycle
The network task SHALL create, configure, and destroy the MQTT client handle. All failure paths after `esp_mqtt_client_init()` SHALL destroy the handle and reset it to null. In persistent mode the MQTT client SHALL be configured with `disable_auto_reconnect = false` and a bounded reconnect interval, and the task SHALL NOT destroy or stop the client across publish cycles.

#### Scenario: Client handle cleanup on property failure
- **WHEN** `esp_mqtt5_client_set_connect_property()` fails after client init
- **THEN** the network task SHALL call `esp_mqtt_client_destroy()` and set the handle to null

#### Scenario: Client handle cleanup on event registration failure
- **WHEN** `esp_mqtt_client_register_event()` fails after client init
- **THEN** the network task SHALL call `esp_mqtt_client_destroy()` and set the handle to null

#### Scenario: Subsequent publish after failed init
- **WHEN** a previous client init failed and was cleaned up
- **THEN** the next publish attempt SHALL re-create the client from scratch without crashing

#### Scenario: Persistent mode keeps MQTT client alive across cycles
- **WHEN** the system is built with `CONFIG_LOGGER_NETWORK_MODE=persistent`
- **AND** a publish cycle completes successfully
- **THEN** the network task SHALL NOT call `esp_mqtt_client_stop()` or `esp_mqtt_client_destroy()`
- **AND** the next publish cycle SHALL reuse the existing client

#### Scenario: Persistent mode enables MQTT auto-reconnect
- **WHEN** the MQTT client is created in persistent mode
- **THEN** `disable_auto_reconnect` SHALL be set to `false`
- **AND** a bounded reconnect interval SHALL be configured on the client

## ADDED Requirements

### Requirement: Persistent publish cycle SHALL reuse the WiFi link
In persistent mode the network task SHALL NOT tear down WiFi or MQTT between publish cycles. `network::ReleaseConnection()` SHALL remain a no-op for the persistent strategy, and the task SHALL NOT call it expecting WiFi shutdown.

#### Scenario: No WiFi teardown after publish in persistent mode
- **WHEN** a publish cycle completes in persistent mode
- **THEN** the network task SHALL NOT disconnect WiFi
- **AND** `network::IsConnected()` SHALL remain true

#### Scenario: WiFi stays associated between cadence waits
- **WHEN** the network task is waiting for the next publish cadence in persistent mode
- **THEN** WiFi SHALL remain associated and the MQTT client SHALL remain started

### Requirement: Every reconnect occurrence SHALL be enumerated and bounded
The system SHALL identify and bound every reconnect occurrence: boot initial connect, WiFi mid-session disconnect, WiFi `EnsureConnected` failure, MQTT mid-session disconnect, and MQTT connect timeout. Each occurrence SHALL have a single defined backoff owner (persistent WiFi strategy or network task) and a bounded maximum retry interval.

#### Scenario: Boot initial connect
- **WHEN** the system boots in persistent mode
- **THEN** the first WiFi connect SHALL be attempted with the initial backoff interval
- **AND** failure SHALL trigger the persistent WiFi backoff schedule

#### Scenario: WiFi mid-session disconnect
- **WHEN** `WIFI_EVENT_STA_DISCONNECTED` fires after IP was acquired in persistent mode
- **THEN** the persistent WiFi strategy SHALL own the reconnect with its bounded backoff
- **AND** the network task SHALL NOT start its own WiFi connect

#### Scenario: WiFi EnsureConnected failure
- **WHEN** `EnsureConnected()` returns false in persistent mode because the backoff window has not elapsed
- **THEN** the network task SHALL treat the publish cycle as failed
- **AND** it SHALL NOT reset or duplicate the WiFi backoff timer

#### Scenario: MQTT mid-session disconnect
- **WHEN** `MQTT_EVENT_DISCONNECTED` fires in persistent mode
- **THEN** the ESP-IDF MQTT client SHALL auto-reconnect using its configured bounded interval
- **AND** the network task SHALL NOT destroy and recreate the client

#### Scenario: MQTT connect timeout in task loop
- **WHEN** the network task waits for `MQTT_EVENT_CONNECTED` and times out in persistent mode
- **THEN** the network task SHALL apply its own exponential backoff
- **AND** it SHALL NOT tear down WiFi
