## MODIFIED Requirements

### Requirement: Node State Machine Initialization
The node SHALL initialize in the `IDLE` state upon startup. The state machine SHALL support two states: `IDLE` and `DISTURBED`. DSP detector thresholds, short-buffer size, refresh margin, and quiet debounce SHALL be configured via Kconfig using scaled integers where floats are required.

#### Scenario: System Startup
- **WHEN** the IoT node is powered on and monitoring starts
- **THEN** the system enters the `IDLE` state
- **THEN** it calculates only tilt statistics for IDLE window reporting
- **THEN** it pre-allocates compile-time fixed-size buffers for short pre-trigger samples and DISTURBED event samples

#### Scenario: Only two states exist
- **WHEN** the state machine is initialized
- **THEN** the valid states SHALL be `IDLE` and `DISTURBED` only
- **THEN** no `FREE_DECAY` state SHALL exist in the FSM

### Requirement: Transition to DISTURBED
The node SHALL transition to the DISTURBED state when calibrated gyro magnitude or streaming TKEO exceeds configured DSP detector thresholds.

#### Scenario: Gyro magnitude exceeds onset threshold
- **WHEN** the node is in the IDLE state
- **WHEN** calibrated gyro magnitude exceeds `CONFIG_MONITOR_DSP_GMAG_ONSET_X100 / 100.0` dps
- **THEN** the state transitions to DISTURBED immediately
- **THEN** the short buffer is appended to the fixed-size event buffer

#### Scenario: TKEO exceeds high threshold
- **WHEN** the node is in the IDLE state
- **WHEN** streaming TKEO exceeds `CONFIG_MONITOR_DSP_TKEO_HIGH_X10 / 10.0`
- **THEN** the state transitions to DISTURBED immediately
- **THEN** the short buffer is appended to the fixed-size event buffer

#### Scenario: No onset threshold exceeded
- **WHEN** the node is in the IDLE state
- **WHEN** calibrated gyro magnitude is at or below the onset threshold
- **WHEN** streaming TKEO is at or below the high threshold
- **THEN** the node SHALL remain in IDLE

### Requirement: Parameter Calculation and Return to IDLE
The node SHALL perform final IMU event analysis and send data immediately upon leaving the DISTURBED state. DISTURBED SHALL return directly to IDLE.

#### Scenario: Disturbance subsides
- **WHEN** the node is in the DISTURBED state
- **WHEN** streaming TKEO is below `CONFIG_MONITOR_DSP_TKEO_LOW_X10 / 10.0`
- **WHEN** calibrated gyro magnitude is below `CONFIG_MONITOR_DSP_GMAG_QUIET_X100 / 100.0` dps for `CONFIG_MONITOR_DISTURBED_EXIT_DEBOUNCE` consecutive samples
- **THEN** the state transitions directly to IDLE
- **THEN** sway statistics from the DISTURBED event buffer SHALL be published
- **THEN** post-hoc IMU event analysis SHALL be triggered on the stored DISTURBED event buffer

#### Scenario: No intermediate FREE_DECAY state
- **WHEN** the node is in the DISTURBED state
- **WHEN** the disturbance subsides
- **THEN** the node SHALL transition directly to IDLE
- **THEN** the node SHALL NOT pass through any intermediate state

### Requirement: DISTURBED Buffer Refresh
The node SHALL prevent event buffer overflow during prolonged disturbances by publishing intermediate parameters and refreshing the event buffer.

#### Scenario: Buffer Nearing Capacity
- **WHEN** the node is in the `DISTURBED` state
- **WHEN** the event buffer is `N_DPAD` away from being full
- **THEN** intermediate sway statistics SHALL be calculated from the current `DISTURBED` buffer and sent immediately
- **THEN** FFT natural frequency and damping SHALL be reported as zero for the refresh payload
- **THEN** the node immediately resets the event buffer with recent short-buffer samples and continues accumulating in `DISTURBED`

### Requirement: Sway Statistics Spanning DISTURBED and FREE_DECAY
Sway peak-to-peak statistics SHALL be computed over the current `DISTURBED` event buffer only. No `FREE_DECAY` span exists.

#### Scenario: Sway accumulation during DISTURBED
- **WHEN** the node is in the `DISTURBED` state
- **THEN** calibrated gyro samples SHALL contribute to event sway calculations

#### Scenario: Sway reset on return to IDLE
- **WHEN** the node transitions from `DISTURBED` to `IDLE`
- **THEN** the final sway values SHALL reflect the current DISTURBED event span only
- **THEN** the event buffers SHALL be reset for the next disturbance event

#### Scenario: Buffer refresh preserves bounded operation
- **WHEN** the DISTURBED buffer is refreshed due to capacity
- **THEN** intermediate sway statistics SHALL be published
- **THEN** the event buffers SHALL be reset with recent short-buffer samples for the next buffer segment

### Requirement: DISTURBED Exit Debounce
The node SHALL require N consecutive quiet samples before transitioning DISTURBED to IDLE. The debounce count SHALL be configurable via Kconfig (`CONFIG_MONITOR_DISTURBED_EXIT_DEBOUNCE`), default 64 samples.

#### Scenario: Debounce met
- **WHEN** the node is in the DISTURBED state
- **WHEN** streaming TKEO remains below the low threshold
- **WHEN** calibrated gyro magnitude remains below the quiet threshold for 64 consecutive samples
- **THEN** the state SHALL transition to IDLE
- **THEN** post-hoc IMU event analysis SHALL be triggered

#### Scenario: Debounce reset on renewed disturbance
- **WHEN** the node is in the DISTURBED state
- **WHEN** quiet conditions are counted for 50 consecutive samples
- **WHEN** streaming TKEO then exceeds the high threshold or calibrated gyro magnitude reaches the quiet threshold
- **THEN** the debounce counter SHALL reset to zero
- **THEN** the node SHALL remain in DISTURBED

#### Scenario: Kconfig default
- **WHEN** `CONFIG_MONITOR_DISTURBED_EXIT_DEBOUNCE` is not explicitly set
- **THEN** the debounce count SHALL default to 64 samples
