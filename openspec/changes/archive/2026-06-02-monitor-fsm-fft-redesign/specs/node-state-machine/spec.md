## MODIFIED Requirements

### Requirement: Node State Machine Initialization
The node SHALL initialize in the `IDLE` state upon startup, maintaining a short variance buffer. The state machine SHALL support three states: `IDLE`, `DISTURBED`, and `FREE_DECAY`.
- `short_buffer_size`, `K_LOW`, `K_MID`, `K_HIGH`, `K_ABS_MIN_ACCEL_VAR`, and `N_DPAD` SHALL be configured via Kconfig.
- Kconfig parameters for floats SHALL be implemented as scaled integers to comply with ESP-IDF Kconfig limitations.

#### Scenario: System Startup
- **WHEN** the IoT node is powered on and monitoring starts
- **THEN** the system enters the `IDLE` state
- **THEN** it calculates only tilt statistics (mean, variance) and accel_err baseline per 5-minute window
- **THEN** it pre-allocates compile-time fixed-size `std::array` buffers for the short buffer and the accel_err short buffer

### Requirement: Transition to DISTURBED
The node SHALL transition to the `DISTURBED` state based on short-term accelerometer error variance, provided a baseline accel_err variance is available. The transition threshold SHALL be `max(accel_err_baseline_var × K_HIGH, K_ABS_MIN_ACCEL_VAR)`.

#### Scenario: Accel Error Variance Exceeds Threshold
- **WHEN** the node is in the `IDLE` state
- **WHEN** a valid previous 5-minute window accel_err baseline variance is available
- **WHEN** the live accel_err_var of the short buffer exceeds `max(accel_err_baseline_var × K_HIGH, K_ABS_MIN_ACCEL_VAR)`
- **THEN** the state transitions to `DISTURBED`
- **THEN** the short buffer is appended (via `std::copy` or loop, without dynamic allocation) to the 5-minute fixed-size ring buffer

#### Scenario: Variance Below Absolute Floor Despite Relative Threshold
- **WHEN** the node is in the `IDLE` state
- **WHEN** baseline accel_err variance is near-zero
- **WHEN** the live accel_err_var exceeds `accel_err_baseline_var × K_HIGH` but is below `K_ABS_MIN_ACCEL_VAR`
- **THEN** the node SHALL NOT transition to `DISTURBED`
- **THEN** the node remains in `IDLE`

### Requirement: Parameter Calculation and Return to IDLE
The node SHALL perform heavy calculations and send data immediately upon leaving the `DISTURBED` state. Direct return from DISTURBED to IDLE SHALL NOT occur; the node MUST pass through `FREE_DECAY` first.

#### Scenario: Disturbance subsides
- **WHEN** the node is in the `DISTURBED` state
- **WHEN** the accel_err_var of the short buffer drops below `max(accel_err_baseline_var × K_MID, K_ABS_MIN_ACCEL_VAR)` for `CONFIG_MONITOR_FREE_DECAY_DEBOUNCE` consecutive samples
- **THEN** the state transitions to `FREE_DECAY` (not directly to IDLE)
- **THEN** sway statistics (pp_max, pp_mean) from the DISTURBED buffer SHALL be published immediately
- **THEN** the decay buffer SHALL be initialized for frequency/damping analysis

#### Scenario: Return to IDLE from FREE_DECAY
- **WHEN** the node is in the `FREE_DECAY` state
- **WHEN** the accel_err_var drops below `max(accel_err_baseline_var × K_LOW, K_ABS_MIN_ACCEL_VAR)`
- **THEN** the state transitions to `IDLE`
- **THEN** natural frequency and damping ratio results are published from the decay buffer

### Requirement: DISTURBED Buffer Refresh
The node SHALL prevent buffer overflow during prolonged disturbances by refreshing the state and computing intermediate parameters.

#### Scenario: Buffer Nearing Capacity
- **WHEN** the node is in the `DISTURBED` state
- **WHEN** the 5-minute buffer is `N_DPAD` away from being full
- **THEN** sway statistics (pp_max, pp_mean) SHALL be calculated from the current `DISTURBED` buffer and sent immediately
- **THEN** the node immediately resets the buffer to continue accumulating in `DISTURBED`

### Requirement: Absolute Minimum Variance Configuration
The absolute minimum variance floor (`K_ABS_MIN_ACCEL_VAR`) SHALL be configurable via Kconfig as a scaled integer (`CONFIG_MONITOR_ABS_MIN_ACCEL_VAR_X1000000`).
- The default value SHALL be 100 (representing 0.0001 g²).
- The Kconfig parameter SHALL use the x1000000 scaling convention.

#### Scenario: Configuration Applied at Runtime
- **WHEN** the monitor initializes
- **THEN** the absolute minimum accel error variance floor is computed as `CONFIG_MONITOR_ABS_MIN_ACCEL_VAR_X1000000 / 1000000.0f`
- **THEN** this value is used in all state transition threshold comparisons

## ADDED Requirements

### Requirement: Sway Statistics Spanning DISTURBED and FREE_DECAY
Sway peak-to-peak statistics (`pp_max` and `pp_mean`) SHALL accumulate across both `DISTURBED` and `FREE_DECAY` states for a single disturbance event.

#### Scenario: Continuous sway accumulation
- **WHEN** the node transitions from `DISTURBED` to `FREE_DECAY`
- **THEN** the sway pp_max and pp_mean accumulators SHALL NOT be reset
- **THEN** roll and pitch samples during FREE_DECAY SHALL continue contributing to sway statistics

#### Scenario: Sway reset on return to IDLE
- **WHEN** the node transitions from `FREE_DECAY` to `IDLE`
- **THEN** the final sway pp_max and pp_mean SHALL reflect the entire DISTURBED + FREE_DECAY span
- **THEN** the sway accumulators SHALL be reset for the next disturbance event

#### Scenario: Re-excitation preserves sway
- **WHEN** the node transitions from `FREE_DECAY` back to `DISTURBED` (re-excitation)
- **THEN** the sway pp_max and pp_mean accumulators SHALL NOT be reset
- **THEN** accumulation continues in the new DISTURBED session

### Requirement: MonitorResult State Field
The `MonitorResult` struct SHALL include a `state` field indicating which FSM state produced the result.

#### Scenario: State field in IDLE publication
- **WHEN** the node publishes a 5-minute window result from `IDLE`
- **THEN** the MonitorResult `state` field SHALL be `IDLE`

#### Scenario: State field in DISTURBED publication
- **WHEN** the node publishes sway statistics from `DISTURBED` (on refresh or transition to FREE_DECAY)
- **THEN** the MonitorResult `state` field SHALL be `DISTURBED`

#### Scenario: State field in FREE_DECAY publication
- **WHEN** the node publishes natural frequency and damping from `FREE_DECAY`
- **THEN** the MonitorResult `state` field SHALL be `FREE_DECAY`

## REMOVED Requirements

### Requirement: Live Variance Calculation

**Reason**: Replaced by the accel-error short buffer rolling variance defined in `accel-error-state-detection`. The tilt-based live variance is no longer used for state transitions.

**Migration**: State transition logic SHALL use `accel_err_var` from the `accel-error-state-detection` capability instead of tilt variance. Tilt variance continues to be computed for IDLE baseline statistics but is no longer used for transition decisions.
