## MODIFIED Requirements

### Requirement: Post-Hoc Decay Analysis Trigger
The monitor SHALL trigger post-hoc IMU event analysis on the stored DISTURBED event buffers when transitioning from DISTURBED to IDLE. The analysis SHALL retroactively identify the decay onset from calibrated gyro magnitude TKEO energy, select a dominant signed calibrated gyro axis, compute FFT natural frequency, then compute peak-hold envelope damping and confidence from the event segment.

#### Scenario: Normal transition triggers analysis
- **WHEN** the node transitions from `DISTURBED` to `IDLE` after quiet debounce is satisfied
- **THEN** the system SHALL invoke post-hoc IMU event analysis on the stored DISTURBED event buffers
- **THEN** the system SHALL compute FFT natural frequency from the dominant signed calibrated gyro axis
- **THEN** the system SHALL compute peak-hold envelope damping and confidence
- **THEN** the results SHALL be published immediately as part of the DISTURBED->IDLE MonitorResult

#### Scenario: Buffer refresh during long disturbance
- **WHEN** the DISTURBED buffer is refreshed due to capacity
- **THEN** intermediate sway statistics SHALL be published
- **THEN** FFT and damping SHALL NOT be computed on the refreshed buffer
- **THEN** FFT and damping SHALL only be computed on the final DISTURBED->IDLE transition

### Requirement: Per-Axis FFT on Post-Hoc Decay Region
The monitor SHALL compute FFT natural frequency on the dominant signed calibrated gyro axis from the retroactively identified decay region of the DISTURBED event buffer. The result SHALL be a single dominant-axis natural frequency mapped into the existing natural-frequency fields.

#### Scenario: Decay region identified with dominant gyro axis
- **WHEN** post-hoc analysis identifies a decay region in the DISTURBED event buffer
- **WHEN** a dominant signed calibrated gyro axis is selected from integrated sway
- **THEN** the system SHALL detrend the signed gyro decay segment
- **THEN** the system SHALL apply a Hann window
- **THEN** the system SHALL compute FFT on the windowed segment
- **THEN** the system SHALL publish the selected frequency in `natural_freq_hz`

#### Scenario: No decay region identified
- **WHEN** post-hoc analysis fails to identify a usable decay region in the DISTURBED event buffer
- **THEN** `natural_freq_hz` SHALL be set to 0.0f
- **THEN** `natural_freq_roll_hz` SHALL be set to 0.0f
- **THEN** `natural_freq_pitch_hz` SHALL be set to 0.0f
- **THEN** `roll_damping_ratio` and `pitch_damping_ratio` SHALL be set to 0.0f
- **THEN** `damping_confidence` SHALL be set to `"low"`

#### Scenario: Insufficient dominant-axis data
- **WHEN** a decay region exists
- **WHEN** the dominant signed gyro decay segment has too few samples for FFT
- **THEN** `natural_freq_hz` SHALL be set to 0.0f
- **THEN** damping ratio SHALL be set to 0.0f
- **THEN** `damping_confidence` SHALL be set to `"low"`

### Requirement: Bounded Firmware Natural Frequency Search
The monitor SHALL select natural frequency only from FFT bins inside a configurable modal search band.

#### Scenario: Default modal search band
- **WHEN** firmware computes natural frequency for a post-hoc IMU event decay region
- **THEN** the default search band SHALL be 0.5 Hz to 12.0 Hz
- **THEN** bins outside the search band SHALL NOT be selected as the natural frequency

#### Scenario: Search band outside available bins
- **WHEN** the configured search band contains no valid non-DC FFT bins
- **THEN** `natural_freq_hz` SHALL be set to 0.0f
- **THEN** damping ratio SHALL be set to 0.0f
- **THEN** `damping_confidence` SHALL be set to `"low"`

### Requirement: Firmware Half Peak-to-Peak Damping Envelope
The monitor SHALL compute damping regression from a peak-hold envelope of calibrated gyro magnitude over the identified decay region.

#### Scenario: Valid peak-hold envelope
- **WHEN** decay quality is not none
- **WHEN** natural frequency is positive
- **WHEN** the bounded envelope fit has enough samples, enough cycles, and sufficient amplitude drop
- **THEN** damping ratio SHALL be computed by linear regression of `ln(envelope)` versus time
- **THEN** `damping_confidence` SHALL be `"high"`, `"medium"`, or `"low"` based on fit quality gates

#### Scenario: Envelope fit gates fail
- **WHEN** the envelope fit has too few samples, too few cycles, insufficient amplitude drop, invalid slope, or invalid natural frequency
- **THEN** damping ratio SHALL be set to 0.0f
- **THEN** `damping_confidence` SHALL be set to `"low"`

### Requirement: Firmware Centerline Modal Configuration
The monitor SHALL expose IMU event analysis thresholds through Kconfig without removing existing runtime payload fields.

#### Scenario: Default detector thresholds
- **WHEN** firmware uses default monitor configuration
- **THEN** TKEO high threshold SHALL default to 40.0
- **THEN** TKEO low threshold SHALL default to 5.0
- **THEN** gyro magnitude onset threshold SHALL default to 2.0 dps
- **THEN** gyro magnitude quiet threshold SHALL default to 1.5 dps

#### Scenario: Default frequency bounds
- **WHEN** firmware uses default monitor configuration
- **THEN** modal FFT minimum frequency SHALL be 0.5 Hz
- **THEN** modal FFT maximum frequency SHALL be 12.0 Hz

