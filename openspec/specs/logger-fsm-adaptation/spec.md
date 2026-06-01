# logger-fsm-adaptation Specification

## Purpose
TBD - created by archiving change adapt-components-new-fsm. Update Purpose after archive.
## Requirements
### Requirement: Logger transmits event-driven parameters
The logger SHALL transmit parameter payloads immediately when provided by the monitor FSM.

#### Scenario: Submitting parameters upon state transition
- **WHEN** the node transitions from `DISTURBED` to `IDLE`
- **THEN** the logger publishes the parameter payload via MQTT

#### Scenario: Submitting parameters on refresh
- **WHEN** the node is in prolonged `DISTURBED` state and emits a refresh parameter block
- **THEN** the logger publishes the parameter payload via MQTT

