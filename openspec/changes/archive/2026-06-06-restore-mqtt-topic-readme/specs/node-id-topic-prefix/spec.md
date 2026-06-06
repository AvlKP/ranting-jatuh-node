## ADDED Requirements

### Requirement: Node ID auto-generation on first boot

The system SHALL generate a unique human-readable identifier for each node on first boot using `esp_random()` to select an adjective and a noun from predefined word pools. The generated ID SHALL be persisted in NVS under namespace `"logger"` key `"node_id"`. The ID SHALL be at most 32 characters including the hyphen separator.

#### Scenario: First boot with empty NVS

- **WHEN** the node boots for the first time and NVS key `"node_id"` does not exist
- **THEN** the system generates a new adjective-noun ID (e.g. `quiet-pine`) using `esp_random()`
- **AND** writes the ID to NVS key `"node_id"`
- **AND** uses the generated ID for all subsequent MQTT publishes

#### Scenario: Subsequent boot with NVS populated

- **WHEN** the node boots and NVS key `"node_id"` exists with value `"bold-oak"`
- **THEN** the system reads and uses `"bold-oak"` as the node ID
- **AND** does not generate a new ID

#### Scenario: NVS read failure

- **WHEN** the node boots and NVS read of key `"node_id"` fails with an error other than `ESP_ERR_NVS_NOT_FOUND`
- **THEN** the system falls back to generating a new ID and attempts to write it to NVS

### Requirement: Kconfig compile-time node ID override

The system SHALL provide a `CONFIG_LOGGER_NODE_ID` Kconfig option that, when set to a non-empty string, overrides all NVS-based node ID resolution.

#### Scenario: Kconfig node ID is set

- **WHEN** `CONFIG_LOGGER_NODE_ID` is configured as `"factory-node-1"` (non-empty)
- **THEN** the system uses `"factory-node-1"` as the node ID
- **AND** does not read from or write to NVS for the node ID

#### Scenario: Kconfig node ID is empty

- **WHEN** `CONFIG_LOGGER_NODE_ID` is empty (default `""`)
- **THEN** the system falls through to NVS-based resolution
- **AND** behaves identically to the auto-generation requirement

#### Scenario: Kconfig override with NVS already populated

- **WHEN** `CONFIG_LOGGER_NODE_ID` is set to `"factory-node-1"` and NVS already contains `"quiet-pine"`
- **THEN** the system uses `"factory-node-1"` and ignores the NVS value

### Requirement: MQTT topics include node ID prefix

All MQTT publish topics SHALL follow the format `ranting/{node_id}/{datatype}` where `{datatype}` is one of `parameters`, `failures`, or `verify` (for the verify utility).

#### Scenario: Parameter topic published

- **WHEN** the logger publishes parameter data for a node with ID `bold-oak`
- **THEN** the MQTT topic is `ranting/bold-oak/parameters`

#### Scenario: Failure topic published

- **WHEN** the logger publishes a failure notification for a node with ID `quiet-pine`
- **THEN** the MQTT topic is `ranting/quiet-pine/failures`

#### Scenario: Verify utility publishes

- **WHEN** the verify utility (`main/verify.cpp`) publishes a test message for a node with ID `bold-oak`
- **THEN** the MQTT topic is `ranting/bold-oak/verify`

### Requirement: Dashboard status API exposes node ID

The dashboard `/api/status` JSON response SHALL include a `"node_id"` field containing the resolved node ID string.

#### Scenario: Status endpoint returns node ID

- **WHEN** an HTTP client requests `/api/status` on a node with ID `bold-oak`
- **THEN** the JSON response body contains `"node_id":"bold-oak"`

#### Scenario: Node ID present alongside other status fields

- **WHEN** an HTTP client requests `/api/status`
- **THEN** the `"node_id"` field appears alongside existing fields (`wifi_connected`, `mqtt_connected`, `heap_free`, `node_state`, etc.)

#### Scenario: Node ID changes across reboots only if Kconfig changes

- **WHEN** the node reboots without changing `CONFIG_LOGGER_NODE_ID` or wiping NVS
- **THEN** the `"node_id"` in the status response is identical to the previous boot

### Requirement: Node ID accessible internally by logger and dashboard

The logger component SHALL expose `GetNodeId()` and `GetTopic(datatype)` functions through its internal header for use by other components (dashboard, verify).

#### Scenario: Dashboard calls GetNodeId

- **WHEN** the dashboard needs the node ID for the status JSON
- **THEN** `GetNodeId()` returns the resolved node ID string (same value used in MQTT topics)

#### Scenario: Verify utility calls GetTopic

- **WHEN** the verify utility needs the MQTT topic for its test message
- **THEN** `GetTopic("verify")` returns `ranting/{node_id}/verify`

#### Scenario: Multiple calls to GetTopic with same datatype use cache

- **WHEN** `GetTopic("parameters")` is called twice in succession
- **THEN** the second call returns the cached pointer without reformatting the string
