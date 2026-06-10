## MODIFIED Requirements

### Requirement: Logger transmits event-driven parameters
The logger SHALL transmit parameter payloads immediately when provided by the monitor FSM for `IDLE` and `DISTURBED` results.

#### Scenario: Submitting parameters upon DISTURBED exit
- **WHEN** the node transitions from `DISTURBED` to `IDLE`
- **THEN** the logger publishes the parameter payload via MQTT

#### Scenario: Submitting parameters on refresh
- **WHEN** the node is in prolonged `DISTURBED` state and emits a refresh parameter block
- **THEN** the logger publishes the parameter payload via MQTT

#### Scenario: Submitting IDLE window parameters
- **WHEN** the monitor emits an `IDLE` parameter block
- **THEN** the logger publishes the parameter payload via MQTT

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

