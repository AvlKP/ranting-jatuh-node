## ADDED Requirements

### Requirement: Realtime IMU Event Detector
The monitor SHALL process calibrated IMU samples through a realtime gyro magnitude and TKEO disturbance detector at the configured 52 Hz polling rate.

#### Scenario: Entering DISTURBED from gyro magnitude
- **WHEN** the monitor is in `IDLE`
- **WHEN** calibrated gyro magnitude exceeds the configured onset threshold
- **THEN** the monitor SHALL enter `DISTURBED`
- **THEN** the monitor SHALL begin storing calibrated event samples

#### Scenario: Entering DISTURBED from TKEO
- **WHEN** the monitor is in `IDLE`
- **WHEN** the streaming TKEO value exceeds the configured high threshold
- **THEN** the monitor SHALL enter `DISTURBED`
- **THEN** the monitor SHALL begin storing calibrated event samples

#### Scenario: Realtime work is bounded
- **WHEN** one IMU sample is processed
- **THEN** detector work SHALL use fixed state and O(1) operations
- **THEN** detector work SHALL NOT allocate heap memory

### Requirement: Event Sample Buffering
The monitor SHALL keep fixed-capacity buffers for calibrated gyro axes, calibrated accelerometer axes, and calibrated gyro magnitude for the current event.

#### Scenario: Event samples stored
- **WHEN** the monitor is in `DISTURBED`
- **THEN** each processed sample SHALL append calibrated `gx`, `gy`, `gz`, `ax`, `ay`, `az`, and `gmag` to the event buffers

#### Scenario: Pre-trigger samples preserved
- **WHEN** the monitor transitions from `IDLE` to `DISTURBED`
- **THEN** recent short-buffer samples SHALL be copied into the event buffers before subsequent DISTURBED samples

#### Scenario: Event buffer capacity is fixed
- **WHEN** the firmware is built
- **THEN** event buffer capacity SHALL be compile-time bounded
- **THEN** the implementation SHALL NOT use dynamic allocation for event storage

### Requirement: IMU Event Parameter Extraction
On final `DISTURBED->IDLE`, the monitor SHALL extract IMU event parameters from the stored event segment without classifying the event.

#### Scenario: Final event extraction
- **WHEN** the node transitions from `DISTURBED` to `IDLE`
- **THEN** the monitor SHALL identify decay onset using non-negative TKEO burst detection over gyro magnitude
- **THEN** the monitor SHALL select a dominant signed gyro axis using the largest calibrated gyro-integrated sway
- **THEN** the monitor SHALL compute natural frequency using FFT on the dominant signed gyro decay segment
- **THEN** the monitor SHALL compute damping ratio and damping confidence from the peak-hold gyro magnitude envelope

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

