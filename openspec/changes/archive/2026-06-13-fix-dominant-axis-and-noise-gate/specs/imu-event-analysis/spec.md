## MODIFIED Requirements

### Requirement: IMU Event Parameter Extraction
On final `DISTURBED->IDLE`, the monitor SHALL extract IMU event parameters from the stored event segment without classifying the event.

#### Scenario: Final event extraction
- **WHEN** the node transitions from `DISTURBED` to `IDLE`
- **THEN** the monitor SHALL identify decay onset using non-negative TKEO burst detection over gyro magnitude
- **THEN** the monitor SHALL select a dominant signed gyro axis using the largest peak-to-peak angular displacement per axis, computed by integrating each calibrated gyro axis over the event segment and taking the difference between maximum and minimum cumulative angle
- **THEN** the monitor SHALL compute natural frequency using FFT on the dominant signed gyro decay segment
- **THEN** the monitor SHALL check the noise gate: if peak gyro magnitude is below the configured threshold, skip damping and publish zero ratio with "low" confidence
- **THEN** the monitor SHALL compute damping ratio and damping confidence from the peak-hold gyro magnitude envelope when the noise gate passes

#### Scenario: No event classification
- **WHEN** event extraction completes
- **THEN** the monitor SHALL NOT classify the event
- **THEN** the monitor SHALL NOT publish event type, onset index, offset index, peak gyro magnitude, or event duration

#### Scenario: Low-quality event
- **WHEN** decay onset, natural frequency, or envelope fitting gates fail
- **THEN** the monitor SHALL publish zero natural frequency and damping ratio where applicable
- **THEN** the monitor SHALL publish `damping_confidence` as `"low"`

### Requirement: Dominant-Axis Result Mapping
The monitor SHALL map the single dominant-axis event result into the existing MonitorResult numeric fields for payload compatibility.

#### Scenario: Natural frequency mapped
- **WHEN** dominant-axis FFT natural frequency is positive
- **THEN** `natural_freq_hz` SHALL contain that frequency
- **THEN** `natural_freq_roll_hz` SHALL mirror `natural_freq_hz`
- **THEN** `natural_freq_pitch_hz` SHALL mirror `natural_freq_hz`

#### Scenario: Damping mapped
- **WHEN** envelope damping produces a damping ratio
- **THEN** `roll_damping_ratio` SHALL contain that damping ratio
- **THEN** `pitch_damping_ratio` SHALL mirror `roll_damping_ratio`
- **THEN** `damping_confidence` SHALL contain the envelope fit confidence
