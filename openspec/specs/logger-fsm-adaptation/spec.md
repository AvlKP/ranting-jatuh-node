# logger-fsm-adaptation Specification

## Purpose
TBD - created by archiving change adapt-components-new-fsm. Update Purpose after archive.
## Requirements
### Requirement: Logger transmits event-driven parameters
The logger SHALL transmit parameter payloads immediately when provided by the monitor FSM, for all three states: `IDLE`, `DISTURBED`, and `FREE_DECAY`.

#### Scenario: Submitting parameters upon DISTURBED to FREE_DECAY transition
- **WHEN** the node transitions from `DISTURBED` to `FREE_DECAY`
- **THEN** the logger publishes the sway parameter payload via MQTT

#### Scenario: Submitting parameters upon FREE_DECAY exit
- **WHEN** the node exits `FREE_DECAY` to `IDLE` (normal or timeout)
- **THEN** the logger publishes the natural frequency and damping parameter payload via MQTT

#### Scenario: Submitting parameters on refresh
- **WHEN** the node is in prolonged `DISTURBED` state and emits a refresh parameter block
- **THEN** the logger publishes the parameter payload via MQTT

### Requirement: Logger formats per-axis frequency fields
The logger SHALL format `natural_freq_roll_hz` and `natural_freq_pitch_hz` as separate numeric fields in the MQTT JSON payload.

#### Scenario: FREE_DECAY payload formatting
- **WHEN** the logger receives a MonitorResult from `FREE_DECAY`
- **THEN** the MQTT JSON payload SHALL contain `natural_freq_roll_hz` as a float field
- **THEN** the MQTT JSON payload SHALL contain `natural_freq_pitch_hz` as a float field
- **THEN** the MQTT JSON payload SHALL contain `damping_ratio_roll` as a float field
- **THEN** the MQTT JSON payload SHALL contain `damping_ratio_pitch` as a float field

### Requirement: Logger formats state field
The logger SHALL include the `state` field from MonitorResult in the MQTT JSON payload.

#### Scenario: State field in MQTT message
- **WHEN** the logger publishes any MonitorResult via MQTT
- **THEN** the JSON payload SHALL contain a `state` field with value `"IDLE"`, `"DISTURBED"`, or `"FREE_DECAY"`
- **THEN** the `state` field SHALL match the MonitorResult `state` field exactly

