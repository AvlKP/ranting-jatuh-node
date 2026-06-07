# free-decay-analysis Specification

## Purpose
TBD - created by archiving change monitor-fsm-fft-redesign. Update Purpose after archive.
## Requirements
### Requirement: Post-Hoc Decay Analysis Trigger
The monitor SHALL trigger post-hoc centerline modal analysis on the stored DISTURBED buffer when transitioning from DISTURBED to IDLE. The analysis SHALL retroactively identify the decay region within the buffer, estimate a moving centerline from lobe-collapsed peak/trough pairs, compute residual tilt, then compute FFT and damping from the centerline-compensated data.

#### Scenario: Normal transition triggers analysis
- **WHEN** the node transitions from `DISTURBED` to `IDLE` (debounce satisfied)
- **THEN** the system SHALL invoke post-hoc decay detection on the stored DISTURBED buffer
- **THEN** the system SHALL compute centerline-compensated FFT and half peak-to-peak damping on the identified decay region
- **THEN** the results SHALL be published immediately as part of the DISTURBED→IDLE MonitorResult

#### Scenario: Buffer refresh during long disturbance
- **WHEN** the DISTURBED buffer is refreshed due to capacity
- **THEN** intermediate sway statistics SHALL be published
- **THEN** FFT and damping SHALL NOT be computed on the refreshed buffer
- **THEN** FFT and damping SHALL only be computed on the final DISTURBED→IDLE transition

### Requirement: Per-Axis FFT on Post-Hoc Decay Region
The monitor SHALL compute FFT on roll and pitch residual signals independently from the retroactively identified decay region of the DISTURBED buffer, producing separate natural frequency results. Each residual signal SHALL be computed by subtracting the axis centerline estimated from valid peak/trough pairs.

#### Scenario: Decay region identified with valid centerline pairs
- **WHEN** post-hoc analysis identifies a decay region in the DISTURBED buffer
- **WHEN** at least two valid centerline pairs are available for an axis
- **THEN** the system SHALL compute the axis residual signal as raw tilt minus interpolated centerline
- **THEN** the system SHALL compute FFT on the residual decay segment, producing the corresponding `natural_freq_*_hz` field
- **THEN** each FFT SHALL use the same adaptive window sizing as the existing FFT implementation (Welch for ≥1024, zero-pad for shorter)

#### Scenario: No decay region identified
- **WHEN** post-hoc analysis fails to identify a valid decay region in the DISTURBED buffer
- **THEN** `natural_freq_roll_hz` SHALL be set to 0.0f
- **THEN** `natural_freq_pitch_hz` SHALL be set to 0.0f
- **THEN** `damping_ratio_roll` and `damping_ratio_pitch` SHALL be set to 0.0f

#### Scenario: Insufficient centerline pairs for one axis
- **WHEN** a valid decay region exists
- **WHEN** an axis has fewer than two valid centerline pairs
- **THEN** the corresponding `natural_freq_*_hz` field SHALL be set to 0.0f
- **THEN** the corresponding damping ratio SHALL be set to 0.0f

### Requirement: Bounded Firmware Natural Frequency Search
The monitor SHALL select natural frequency only from FFT or PSD bins inside a configurable modal search band.

#### Scenario: Default modal search band
- **WHEN** firmware computes natural frequency for a post-hoc decay region
- **THEN** the default search band SHALL be 0.5 Hz to 12.0 Hz
- **THEN** bins outside the search band SHALL NOT be selected as the natural frequency

#### Scenario: Search band outside available bins
- **WHEN** the configured search band contains no valid non-DC FFT or PSD bins
- **THEN** the corresponding natural frequency SHALL be set to 0.0f
- **THEN** the corresponding damping ratio SHALL be set to 0.0f

### Requirement: Firmware Half Peak-to-Peak Damping Envelope
The monitor SHALL compute damping regression from half peak-to-peak amplitudes derived from adjacent opposite lobe-collapsed extrema.

#### Scenario: Valid pair envelope
- **WHEN** at least four half peak-to-peak amplitudes are available from valid centerline pairs
- **WHEN** the corresponding natural frequency is positive
- **THEN** damping ratio SHALL be computed by linear regression of ln(amplitude) versus time

#### Scenario: Too few pair amplitudes
- **WHEN** fewer than four half peak-to-peak amplitudes are available for an axis
- **THEN** the corresponding damping ratio SHALL be set to 0.0f

### Requirement: Firmware Centerline Modal Configuration
The monitor SHALL expose modal-analysis thresholds through Kconfig without changing runtime payload schemas.

#### Scenario: Default centerline thresholds
- **WHEN** firmware uses default monitor configuration
- **THEN** centerline pair minimum amplitude SHALL be 0.05 degrees
- **THEN** lobe reversal threshold SHALL be 0.10 degrees

#### Scenario: Default frequency bounds
- **WHEN** firmware uses default monitor configuration
- **THEN** modal FFT minimum frequency SHALL be 0.5 Hz
- **THEN** modal FFT maximum frequency SHALL be 12.0 Hz

