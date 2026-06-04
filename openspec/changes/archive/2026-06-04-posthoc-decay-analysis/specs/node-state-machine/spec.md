# node-state-machine Delta Specification

**Change:** posthoc-decay-analysis
**Base Spec:** openspec/specs/node-state-machine/spec.md

## MODIFIED Requirements

### Requirement: Node State Machine Initialization
The node SHALL initialize in the `IDLE` state upon startup, maintaining a short variance buffer. The state machine SHALL support two states: `IDLE` and `DISTURBED`.
- `short_buffer_size`, `K_LOW`, `K_HIGH`, `K_ABS_MIN_ACCEL_VAR`, and `N_DPAD` SHALL be configured via Kconfig.
- Kconfig parameters for floats SHALL be implemented as scaled integers to comply with ESP-IDF Kconfig limitations.
- `K_MID` SHALL be removed from Kconfig.

#### Scenario: System Startup
- **WHEN** the IoT node is powered on and monitoring starts
- **THEN** the system enters the `IDLE` state
- **THEN** it calculates only tilt statistics (mean, variance) and accel_err baseline per 5-minute window
- **THEN** it pre-allocates compile-time fixed-size `std::array` buffers for the short buffer and the accel_err short buffer

#### Scenario: Only two states exist
- **WHEN** the state machine is initialized
- **THEN** the valid states SHALL be `IDLE` and `DISTURBED` only
- **THEN** no `FREE_DECAY` state SHALL exist in the FSM

### Requirement: Parameter Calculation and Return to IDLE
The node SHALL perform heavy calculations and send data immediately upon leaving the `DISTURBED` state. DISTURBED SHALL return directly to IDLE. On exit from DISTURBED, post-hoc decay analysis SHALL run on the stored DISTURBED buffer.

#### Scenario: Disturbance subsides
- **WHEN** the node is in the `DISTURBED` state
- **WHEN** the accel_err_var of the short buffer drops below `max(accel_err_baseline_var × K_LOW, K_ABS_MIN_ACCEL_VAR)` for `CONFIG_MONITOR_DISTURBED_EXIT_DEBOUNCE` consecutive samples
- **THEN** the state transitions directly to `IDLE`
- **THEN** sway statistics (pp_max, pp_mean) from the DISTURBED buffer SHALL be published
- **THEN** post-hoc decay analysis (FFT + damping) SHALL be triggered on the stored DISTURBED buffer

#### Scenario: No intermediate FREE_DECAY state
- **WHEN** the node is in the `DISTURBED` state
- **WHEN** the disturbance subsides
- **THEN** the node SHALL transition directly to `IDLE`
- **THEN** the node SHALL NOT pass through any intermediate state

### Requirement: Sway Statistics Spanning DISTURBED and FREE_DECAY
Sway peak-to-peak statistics (`pp_max` and `pp_mean`) SHALL be computed over the entire `DISTURBED` buffer only. No `FREE_DECAY` span is included.

#### Scenario: Sway accumulation during DISTURBED
- **WHEN** the node is in the `DISTURBED` state
- **THEN** roll and pitch samples SHALL contribute to sway pp_max and pp_mean accumulators

#### Scenario: Sway reset on return to IDLE
- **WHEN** the node transitions from `DISTURBED` to `IDLE`
- **THEN** the final sway pp_max and pp_mean SHALL reflect the entire DISTURBED span only
- **THEN** the sway accumulators SHALL be reset for the next disturbance event

#### Scenario: Buffer refresh preserves sway
- **WHEN** the DISTURBED buffer is refreshed due to capacity
- **THEN** intermediate sway statistics SHALL be published
- **THEN** the sway accumulators SHALL be reset for the next buffer segment

### Requirement: MonitorResult State Field
The `MonitorResult` struct SHALL include a `state` field indicating which FSM state produced the result.

#### Scenario: State field in IDLE publication
- **WHEN** the node publishes a 5-minute window result from `IDLE`
- **THEN** the MonitorResult `state` field SHALL be `IDLE`

#### Scenario: State field in DISTURBED publication
- **WHEN** the node publishes sway statistics or post-hoc decay results from `DISTURBED`
- **THEN** the MonitorResult `state` field SHALL be `DISTURBED`

## ADDED Requirements

### Requirement: DISTURBED Exit Debounce
The node SHALL require N consecutive samples below `max(accel_err_baseline_var × K_LOW, K_ABS_MIN_ACCEL_VAR)` before transitioning DISTURBED→IDLE. The debounce count SHALL be configurable via Kconfig (`CONFIG_MONITOR_DISTURBED_EXIT_DEBOUNCE`), default 64 samples.

#### Scenario: Debounce met
- **WHEN** the node is in the `DISTURBED` state
- **WHEN** `accel_err_var` remains below `max(accel_err_baseline_var × K_LOW, K_ABS_MIN_ACCEL_VAR)` for 64 consecutive samples
- **THEN** the state SHALL transition to `IDLE`
- **THEN** post-hoc decay analysis SHALL be triggered

#### Scenario: Debounce reset on spike
- **WHEN** the node is in the `DISTURBED` state
- **WHEN** `accel_err_var` drops below the threshold for 50 consecutive samples
- **WHEN** `accel_err_var` then exceeds the threshold on the 51st sample
- **THEN** the debounce counter SHALL reset to zero
- **THEN** the node SHALL remain in `DISTURBED`

#### Scenario: Kconfig default
- **WHEN** `CONFIG_MONITOR_DISTURBED_EXIT_DEBOUNCE` is not explicitly set
- **THEN** the debounce count SHALL default to 64 samples


