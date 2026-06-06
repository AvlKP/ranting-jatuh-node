## ADDED Requirements

### Requirement: Node ID auto-generation on first boot

The system SHALL generate a unique human-readable node identifier on first boot when no pre-existing ID is configured or persisted.

The node ID format SHALL be `{adjective}-{noun}` where both parts are randomly selected from fixed word lists embedded in firmware. The total length MUST NOT exceed 32 characters.

The system SHALL use `esp_random()` (hardware RNG) to select words.

#### Scenario: First boot with no Kconfig override

- **WHEN** the device boots for the first time and `CONFIG_LOGGER_NODE_ID` is empty
- **AND** no `node_id` exists in NVS namespace `logger`
- **THEN** the system generates a random `{adjective}-{noun}` ID (e.g. `quiet-pine`)
- **AND** stores it in NVS namespace `logger` under key `node_id` as a null-terminated string

#### Scenario: NVS node_id already exists

- **WHEN** the device boots and `CONFIG_LOGGER_NODE_ID` is empty
- **AND** a `node_id` exists in NVS namespace `logger`
- **THEN** the system uses the stored NVS value
- **AND** does NOT generate a new ID
- **AND** does NOT overwrite the existing NVS value

#### Scenario: NVS corrupted or erased

- **WHEN** the device boots and `CONFIG_LOGGER_NODE_ID` is empty
- **AND** NVS read of `node_id` fails (partition erased or corrupted)
- **THEN** the system generates a new random `{adjective}-{noun}` ID
- **AND** stores the new ID in NVS

### Requirement: Node ID compile-time override via Kconfig

The system SHALL support a Kconfig option `CONFIG_LOGGER_NODE_ID` that overrides auto-generation.

When `CONFIG_LOGGER_NODE_ID` is non-empty at compile time, the system MUST use this value as the node ID and MUST NOT read or write `node_id` to NVS.

#### Scenario: Kconfig node_id set to non-empty value

- **WHEN** `CONFIG_LOGGER_NODE_ID` is set to a non-empty string (e.g. `pole-12`)
- **THEN** the system uses `pole-12` as the node ID
- **AND** does NOT read NVS for `node_id`
- **AND** does NOT generate a random ID

#### Scenario: Kconfig node_id is empty (default)

- **WHEN** `CONFIG_LOGGER_NODE_ID` is empty string `""`
- **THEN** the system falls through to NVS-based ID resolution (see Requirement: Node ID auto-generation on first boot)

### Requirement: MQTT topics include node ID prefix

All MQTT publish topics MUST use the format `ranting/{node_id}/{datatype}` where `{datatype}` is one of `parameters`, `failures`, or `verify`.

The system SHALL construct topic strings dynamically at publish time using a helper function that combines a `ranting/` base prefix, the resolved node ID, and the datatype suffix.

#### Scenario: Publishing parameters with node ID prefix

- **WHEN** the logger publishes a parameter batch
- **AND** node ID is `quiet-pine`
- **THEN** the MQTT topic SHALL be `ranting/quiet-pine/parameters`

#### Scenario: Publishing failures with node ID prefix

- **WHEN** the logger publishes a failure event
- **AND** node ID is `pole-12`
- **THEN** the MQTT topic SHALL be `ranting/pole-12/failures`

#### Scenario: Publishing verification with node ID prefix

- **WHEN** startup verification publishes an MQTT test message
- **AND** node ID is `bold-oak`
- **THEN** the MQTT topic SHALL be `ranting/bold-oak/verify`

### Requirement: Dashboard status API exposes node ID

The dashboard HTTP status API (`/api/status` endpoint) MUST include the resolved node ID in its JSON response.

The node ID SHALL appear as a top-level string field `"node_id"` in the JSON object.

#### Scenario: Dashboard status includes node_id

- **WHEN** a client requests `/api/status` via HTTP
- **THEN** the JSON response includes `"node_id":"quiet-pine"` (with the actual resolved node ID)

### Requirement: Node ID accessible internally by logger and dashboard components

The resolved node ID SHALL be accessible as a compile-time constant or runtime-accessible function from both the logger MQTT module and the dashboard module without circular dependencies.

#### Scenario: Logger MQTT module reads node ID

- **WHEN** `PublishParameters()` is called
- **THEN** the function retrieves the node ID to construct the publish topic
- **AND** the retrieval does NOT require an MQTT connection or WiFi state

#### Scenario: Dashboard module reads node ID

- **WHEN** the dashboard status handler builds the JSON response
- **THEN** it retrieves the node ID for inclusion in the `node_id` field
- **AND** the retrieval is non-blocking and fast
