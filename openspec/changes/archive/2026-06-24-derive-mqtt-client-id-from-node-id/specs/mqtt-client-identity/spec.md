## ADDED Requirements

### Requirement: MQTT client ID derives from node ID

The logger SHALL derive the MQTT client ID from the resolved node ID before creating the ESP-MQTT client. If `CONFIG_LOGGER_MQTT_CLIENT_ID` is non-empty, the final client ID SHALL be `<configured-client-id>-<node-id>`. If `CONFIG_LOGGER_MQTT_CLIENT_ID` is empty, the final client ID SHALL be `<node-id>`.

#### Scenario: Default configured client prefix

- **WHEN** the resolved node ID is `bold-oak`
- **AND** `CONFIG_LOGGER_MQTT_CLIENT_ID` is `ranting-logger`
- **THEN** the MQTT client connects using client ID `ranting-logger-bold-oak`

#### Scenario: Empty configured client prefix

- **WHEN** the resolved node ID is `quiet-pine`
- **AND** `CONFIG_LOGGER_MQTT_CLIENT_ID` is empty
- **THEN** the MQTT client connects using client ID `quiet-pine`

#### Scenario: Two nodes share configured prefix

- **WHEN** one node resolves node ID `bold-oak`
- **AND** another node resolves node ID `quiet-pine`
- **AND** both use `CONFIG_LOGGER_MQTT_CLIENT_ID` as `ranting-logger`
- **THEN** the first node connects using client ID `ranting-logger-bold-oak`
- **AND** the second node connects using client ID `ranting-logger-quiet-pine`
- **AND** the two nodes do not share the same MQTT client ID

### Requirement: MQTT client ID generation is bounded and deterministic

The logger SHALL generate the MQTT client ID using fixed-size storage and bounded formatting. The generated client ID SHALL remain stable across calls and reboots as long as the resolved node ID and configured client prefix do not change.

#### Scenario: Repeated client ID lookup

- **WHEN** `GetClientId()` is called twice after node ID resolution
- **THEN** both calls return the same client ID text
- **AND** no dynamic allocation is required

#### Scenario: Stable node ID across reboot

- **WHEN** a node resolves persisted NVS node ID `bold-oak` before MQTT client creation
- **AND** `CONFIG_LOGGER_MQTT_CLIENT_ID` remains `ranting-logger`
- **THEN** the MQTT client ID remains `ranting-logger-bold-oak`
