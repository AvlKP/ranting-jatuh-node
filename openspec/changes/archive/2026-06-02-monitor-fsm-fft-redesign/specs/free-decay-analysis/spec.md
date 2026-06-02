# free-decay-analysis Specification

## Purpose
FREE_DECAY state with dedicated buffer for natural frequency and damping ratio computation from post-disturbance oscillation data. Isolates the unforced decay window so FFT and log-decrement analysis operate on clean free-vibration signals, per axis.

## ADDED Requirements

### Requirement: FREE_DECAY State Entry
The monitor SHALL enter FREE_DECAY from DISTURBED when the accel_err_var drops below `max(accel_err_baseline_var × K_MID, K_ABS_MIN_ACCEL_VAR)` for a configurable number of consecutive samples (debounce).
- The debounce count SHALL be configured via Kconfig (`CONFIG_MONITOR_FREE_DECAY_DEBOUNCE`), default 128.
- `K_MID` SHALL be configured via Kconfig as a scaled integer.

#### Scenario: Smooth transition after debounce
- **WHEN** the node is in the `DISTURBED` state
- **WHEN** `accel_err_var` remains below `max(accel_err_baseline_var × K_MID, K_ABS_MIN_ACCEL_VAR)` for 128 consecutive samples
- **THEN** the state SHALL transition to `FREE_DECAY`

#### Scenario: Debounce reset on spike
- **WHEN** the node is in the `DISTURBED` state
- **WHEN** `accel_err_var` drops below the threshold for 100 consecutive samples
- **WHEN** `accel_err_var` then exceeds the threshold on the 101st sample
- **THEN** the debounce counter SHALL reset to zero
- **THEN** the node SHALL remain in `DISTURBED`

### Requirement: FREE_DECAY Buffer Initialization
The monitor SHALL initialize a fresh roll/pitch decay buffer upon entering FREE_DECAY, pre-populated with the current short buffer contents.

#### Scenario: Buffer pre-roll on entry
- **WHEN** the state transitions from `DISTURBED` to `FREE_DECAY`
- **THEN** the system SHALL allocate a decay buffer from the existing fixed-size ring buffer
- **THEN** the decay buffer SHALL be pre-populated with the current short buffer roll and pitch values via copy (no dynamic allocation)
- **THEN** new roll and pitch samples SHALL continue appending to the decay buffer

### Requirement: Per-Axis FFT in FREE_DECAY
The monitor SHALL compute FFT on roll and pitch signals independently during FREE_DECAY, producing separate natural frequency results.

#### Scenario: Per-axis frequency extraction
- **WHEN** FREE_DECAY exits (to IDLE or timeout)
- **THEN** the system SHALL compute FFT on the roll decay buffer, producing `natural_freq_roll_hz`
- **THEN** the system SHALL compute FFT on the pitch decay buffer, producing `natural_freq_pitch_hz`
- **THEN** each FFT SHALL use the same adaptive window sizing as the existing FFT implementation (Welch for ≥1024, zero-pad for shorter)

#### Scenario: Natural frequency zeroed during IDLE
- **WHEN** the node is in the `IDLE` state
- **THEN** `natural_freq_roll_hz` SHALL be set to 0.0f
- **THEN** `natural_freq_pitch_hz` SHALL be set to 0.0f

### Requirement: Per-Axis Damping Ratio in FREE_DECAY
The monitor SHALL compute damping ratio from the FREE_DECAY buffer using the log-decrement method, per axis.

#### Scenario: Damping ratio from decay envelope
- **WHEN** FREE_DECAY exits normally
- **THEN** the system SHALL compute `damping_ratio_roll` from the roll decay buffer using log-decrement on successive peaks
- **THEN** the system SHALL compute `damping_ratio_pitch` from the pitch decay buffer using log-decrement on successive peaks

#### Scenario: Insufficient peaks for damping
- **WHEN** the decay buffer contains fewer than 2 identifiable peaks on an axis
- **THEN** the damping ratio for that axis SHALL be set to 0.0f
- **THEN** the system SHALL still publish the natural frequency result for that axis

### Requirement: FREE_DECAY Exit to IDLE
The monitor SHALL transition from FREE_DECAY to IDLE when accel_err_var drops below `max(accel_err_baseline_var × K_LOW, K_ABS_MIN_ACCEL_VAR)`.
- `K_LOW` SHALL be configured via Kconfig as a scaled integer (same scaling convention as existing K parameters).

#### Scenario: Decay complete
- **WHEN** the node is in `FREE_DECAY`
- **WHEN** `accel_err_var` drops below `max(accel_err_baseline_var × K_LOW, K_ABS_MIN_ACCEL_VAR)`
- **THEN** the system SHALL compute per-axis natural frequency and damping ratio from the decay buffer
- **THEN** the system SHALL publish the results immediately
- **THEN** the state SHALL transition to `IDLE`

### Requirement: FREE_DECAY Re-excitation
The monitor SHALL transition from FREE_DECAY back to DISTURBED when accel_err_var exceeds `max(accel_err_baseline_var × K_HIGH, K_ABS_MIN_ACCEL_VAR)`, discarding the current decay buffer.

#### Scenario: Re-excitation during decay
- **WHEN** the node is in `FREE_DECAY`
- **WHEN** `accel_err_var` exceeds `max(accel_err_baseline_var × K_HIGH, K_ABS_MIN_ACCEL_VAR)`
- **THEN** the decay buffer SHALL be discarded (no frequency/damping computation)
- **THEN** the state SHALL transition to `DISTURBED`
- **THEN** sway statistics SHALL continue accumulating from the previous DISTURBED session

### Requirement: FREE_DECAY Timeout
The monitor SHALL enforce a configurable timeout on the FREE_DECAY state to prevent indefinite waiting.
- The timeout duration SHALL be configured via Kconfig (`CONFIG_MONITOR_FREE_DECAY_TIMEOUT_S`), default 120 seconds.

#### Scenario: Timeout exceeded
- **WHEN** the node is in `FREE_DECAY`
- **WHEN** the time spent in FREE_DECAY exceeds `CONFIG_MONITOR_FREE_DECAY_TIMEOUT_S`
- **THEN** the system SHALL compute per-axis natural frequency and damping ratio from the available decay buffer (partial results)
- **THEN** the system SHALL publish the partial results
- **THEN** the state SHALL transition to `IDLE`

### Requirement: FREE_DECAY Result Publication
The monitor SHALL publish natural frequency and damping results upon every FREE_DECAY exit (normal, timeout, or re-excitation does NOT publish).

#### Scenario: Publication on normal exit
- **WHEN** FREE_DECAY exits to IDLE (normal or timeout)
- **THEN** the MonitorResult SHALL contain `natural_freq_roll_hz`, `natural_freq_pitch_hz`, `damping_ratio_roll`, `damping_ratio_pitch`
- **THEN** the MonitorResult `state` field SHALL be `FREE_DECAY`
- **THEN** the result SHALL be sent immediately to the logger
