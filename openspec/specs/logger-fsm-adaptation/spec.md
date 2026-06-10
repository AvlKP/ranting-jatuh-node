# logger-fsm-adaptation Specification

## Purpose
Adapt the logger FSM to handle event-driven parameters and state-specific logging.
## Requirements
### Requirement: Logger transmits event-driven parameters
The logger SHALL write parameter payloads to SD immediately and enqueue them for asynchronous MQTT publishing via the network task. The logger task SHALL NOT perform any WiFi or MQTT operations directly.

#### Scenario: Submitting parameters upon DISTURBED exit
- **WHEN** the node transitions from `DISTURBED` to `IDLE`
- **THEN** the logger writes the parameter payload to SD
- **THEN** the logger appends the JSON payload to the SD outbox for the network task to publish

#### Scenario: Submitting parameters on refresh
- **WHEN** the node is in prolonged `DISTURBED` state and emits a refresh parameter block
- **THEN** the logger writes the parameter payload to SD
- **THEN** the logger appends the JSON payload to the SD outbox for the network task to publish

#### Scenario: Submitting IDLE window parameters
- **WHEN** the monitor emits an `IDLE` parameter block
- **THEN** the logger writes the parameter payload to SD
- **THEN** the logger appends the JSON payload to the SD outbox for the network task to publish

### Requirement: Logger formats per-axis frequency fields
The logger SHALL preserve the current MQTT JSON numeric parameter fields and add only `damping_confidence` for IMU event analysis confidence.

#### Scenario: DISTURBED final payload formatting
- **WHEN** the logger receives a final DISTURBED MonitorResult
- **THEN** the MQTT JSON payload SHALL contain `natural_freq_hz` as a float field
- **THEN** the MQTT JSON payload SHALL contain `natural_freq_roll_hz` as a float field
- **THEN** the MQTT JSON payload SHALL contain `natural_freq_pitch_hz` as a float field
- **THEN** the MQTT JSON payload SHALL contain `roll_damping_ratio` as a float field
- **THEN** the MQTT JSON payload SHALL contain `pitch_damping_ratio` as a float field
- **THEN** the MQTT JSON payload SHALL contain `damping_confidence` as a string field

#### Scenario: No event metadata in payload
- **WHEN** the logger formats any parameter payload
- **THEN** the payload SHALL NOT contain `event_type`
- **THEN** the payload SHALL NOT contain `onset_idx`
- **THEN** the payload SHALL NOT contain `offset_idx`
- **THEN** the payload SHALL NOT contain `peak_gmag`
- **THEN** the payload SHALL NOT contain event duration fields

### Requirement: Logger formats state field
The logger SHALL include the `state` field from MonitorResult in the MQTT JSON payload.

#### Scenario: State field in MQTT message
- **WHEN** the logger publishes any MonitorResult via MQTT
- **THEN** the JSON payload SHALL contain a `state` field with value `"IDLE"` or `"DISTURBED"`
- **THEN** the `state` field SHALL match the MonitorResult `state` field exactly

### Requirement: Logger stores parameter payloads as proper CSV with new fields
The logger SHALL format parameter payloads as valid comma-separated values (CSV) matching the header in the SD card storage file. The logger SHALL include `damping_confidence` in the CSV line and header without adding event classification or event boundary fields.

#### Scenario: Appending parameter to SD card
- **WHEN** the logger processes a MonitorResult for SD card storage
- **THEN** it formats a CSV line containing all current parameter fields plus `damping_confidence` in the order defined by the CSV header
- **THEN** the output SHALL be a valid comma-separated string, not a JSON object

#### Scenario: Header includes confidence
- **WHEN** the logger creates a new parameter CSV file
- **THEN** the CSV header SHALL include `damping_confidence`
- **THEN** the CSV header SHALL NOT include event classification or event boundary columns

### Requirement: Logger writes failure events to SD before network
The logger SHALL write failure events to SD storage unconditionally and immediately, before any network publish attempt is enqueued.

#### Scenario: Failure event SD write ordering
- **WHEN** the logger receives a failure event (free fall or acoustic emission)
- **THEN** it SHALL write the failure CSV to SD storage first
- **THEN** it SHALL write the failure JSON to the SD outbox for the network task to publish

#### Scenario: Failure SD write during network outage
- **WHEN** the logger receives a failure event and WiFi is unreachable
- **THEN** the failure SHALL be written to SD within one task loop tick (~100ms)
- **THEN** the failure SHALL NOT be delayed by any network timeout or retry

