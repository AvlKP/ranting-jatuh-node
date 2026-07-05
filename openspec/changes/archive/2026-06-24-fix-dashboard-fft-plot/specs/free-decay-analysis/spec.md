## MODIFIED Requirements

### Requirement: Per-Axis FFT on Post-Hoc Decay Region
The monitor SHALL compute FFT natural frequency on the dominant signed calibrated gyro axis from the retroactively identified decay region of the DISTURBED event buffer. The result SHALL be a single dominant-axis natural frequency mapped into the existing natural-frequency fields. The same successful FFT SHALL update the dashboard-visible PSD buffer returned by `GetFftData()`.

#### Scenario: Decay region identified with dominant gyro axis
- **WHEN** post-hoc analysis identifies a decay region in the DISTURBED event buffer
- **WHEN** a dominant signed calibrated gyro axis is selected from integrated sway
- **THEN** the system SHALL detrend the signed gyro decay segment
- **THEN** the system SHALL apply a Hann window
- **THEN** the system SHALL compute FFT on the windowed segment
- **THEN** the system SHALL write positive-frequency power values into the dashboard-visible PSD buffer
- **THEN** the system SHALL publish the selected frequency in `natural_freq_hz`

#### Scenario: No decay region identified
- **WHEN** post-hoc analysis fails to identify a usable decay region in the DISTURBED event buffer
- **THEN** `natural_freq_hz` SHALL be set to 0.0f
- **THEN** `natural_freq_roll_hz` SHALL be set to 0.0f
- **THEN** `natural_freq_pitch_hz` SHALL be set to 0.0f
- **THEN** `roll_damping_ratio` and `pitch_damping_ratio` SHALL be set to 0.0f
- **THEN** `damping_confidence` SHALL be set to `"low"`
- **THEN** the dashboard-visible PSD buffer SHALL NOT report new nonzero FFT bins for that failed analysis

#### Scenario: Insufficient dominant-axis data
- **WHEN** a decay region exists
- **WHEN** the dominant signed gyro decay segment has too few samples for FFT
- **THEN** `natural_freq_hz` SHALL be set to 0.0f
- **THEN** damping ratio SHALL be set to 0.0f
- **THEN** `damping_confidence` SHALL be set to `"low"`
- **THEN** the dashboard-visible PSD buffer SHALL be cleared or remain all zero

