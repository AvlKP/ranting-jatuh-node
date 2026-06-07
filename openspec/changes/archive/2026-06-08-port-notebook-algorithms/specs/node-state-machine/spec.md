## MODIFIED Requirements

### Requirement: Transition to DISTURBED
The node SHALL transition to the DISTURBED state when the Chebyshev HPF magnitude of calibrated accelerometer data exceeds a fixed threshold, after the HPF settle period is complete. No baseline variance is required. No K_HIGH/K_LOW multipliers are used.

#### Scenario: HPF Magnitude Exceeds Threshold
- **WHEN** the node is in the IDLE state
- **WHEN** the HPF settle period is complete
- **WHEN** the HPF magnitude `sqrt(hpf_x² + hpf_y² + hpf_z²)` exceeds `CONFIG_MONITOR_HPF_THRESHOLD_X1000 / 1000.0` g
- **THEN** the state transitions to DISTURBED immediately
- **THEN** the short buffer is appended to the 5-minute fixed-size ring buffer

#### Scenario: HPF Magnitude Below Threshold
- **WHEN** the node is in the IDLE state
- **WHEN** the HPF magnitude is below the threshold
- **THEN** the node SHALL remain in IDLE

### Requirement: Parameter Calculation and Return to IDLE
The node SHALL perform heavy calculations and send data immediately upon leaving the DISTURBED state. DISTURBED SHALL return directly to IDLE. On exit from DISTURBED, post-hoc decay analysis SHALL run on the stored DISTURBED buffer.

#### Scenario: Disturbance subsides
- **WHEN** the node is in the DISTURBED state
- **WHEN** the HPF magnitude drops below `CONFIG_MONITOR_HPF_THRESHOLD_X1000 / 1000.0` g for `CONFIG_MONITOR_DISTURBED_EXIT_DEBOUNCE` consecutive samples
- **THEN** the state transitions directly to IDLE
- **THEN** sway statistics (pp_max, pp_mean) from the DISTURBED buffer SHALL be published
- **THEN** post-hoc decay analysis (FFT + damping) SHALL be triggered on the stored DISTURBED buffer

#### Scenario: No intermediate FREE_DECAY state
- **WHEN** the node is in the DISTURBED state
- **WHEN** the disturbance subsides
- **THEN** the node SHALL transition directly to IDLE
- **THEN** the node SHALL NOT pass through any intermediate state

### Requirement: DISTURBED Exit Debounce
The node SHALL require N consecutive samples below the HPF threshold before transitioning DISTURBED to IDLE. The debounce count SHALL be configurable via Kconfig (`CONFIG_MONITOR_DISTURBED_EXIT_DEBOUNCE`), default 64 samples.

#### Scenario: Debounce met
- **WHEN** the node is in the DISTURBED state
- **WHEN** HPF magnitude remains below `CONFIG_MONITOR_HPF_THRESHOLD_X1000 / 1000.0` g for 64 consecutive samples
- **THEN** the state SHALL transition to IDLE
- **THEN** post-hoc decay analysis SHALL be triggered

#### Scenario: Debounce reset on spike
- **WHEN** the node is in the DISTURBED state
- **WHEN** HPF magnitude drops below the threshold for 50 consecutive samples
- **WHEN** HPF magnitude then exceeds the threshold on the 51st sample
- **THEN** the debounce counter SHALL reset to zero
- **THEN** the node SHALL remain in DISTURBED

#### Scenario: Kconfig default
- **WHEN** `CONFIG_MONITOR_DISTURBED_EXIT_DEBOUNCE` is not explicitly set
- **THEN** the debounce count SHALL default to 64 samples

## REMOVED Requirements

### Requirement: Absolute Minimum Variance Configuration
**Reason**: The HPF method uses a single absolute threshold (0.02 g). No variance floor configuration is needed. The `K_ABS_MIN_ACCEL_VAR` concept applies only to variance-based detection.
**Migration**: Remove `CONFIG_MONITOR_ABS_MIN_ACCEL_VAR_X1000000` from Kconfig. Use `CONFIG_MONITOR_HPF_THRESHOLD_X1000` for the detection threshold.
