## ADDED Requirements

### Requirement: Node ID determines MQTT client identity

The resolved node ID SHALL be the unique per-node component of MQTT client identity in addition to MQTT topic prefixes and dashboard status.

#### Scenario: Configured node ID controls client identity

- **WHEN** `CONFIG_LOGGER_NODE_ID` is configured as `factory-node-1`
- **AND** `CONFIG_LOGGER_MQTT_CLIENT_ID` is `ranting-logger`
- **THEN** the MQTT client ID is `ranting-logger-factory-node-1`

#### Scenario: Persisted generated node ID controls client identity

- **WHEN** NVS contains node ID `quiet-pine`
- **AND** `CONFIG_LOGGER_NODE_ID` is empty
- **AND** `CONFIG_LOGGER_MQTT_CLIENT_ID` is `ranting-logger`
- **THEN** the MQTT client ID is `ranting-logger-quiet-pine`

#### Scenario: Node ID remains source of topic identity

- **WHEN** the resolved node ID is `bold-oak`
- **AND** the MQTT client ID is `ranting-logger-bold-oak`
- **THEN** `GetTopic("parameters")` still returns `ranting/bold-oak/parameters`
