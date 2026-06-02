# accel-error-state-detection Specification

## Purpose
TBD - created by archiving change monitor-fsm-fft-redesign. Update Purpose after archive.
## Requirements
### Requirement: Accelerometer Error Metric Computation
The monitor SHALL compute the accelerometer error metric as `|√(ax²+ay²+az²) - 1.0|` for each raw IMU sample, where accelerometer values are in g units.

#### Scenario: Normal gravity reading
- **WHEN** a raw IMU sample reads `ax=0.0, ay=0.0, az=1.0` (device at rest, Z-up)
- **THEN** the accelerometer error metric SHALL be `|√(0²+0²+1²) - 1.0| = 0.0`

#### Scenario: Disturbed reading
- **WHEN** a raw IMU sample reads `ax=0.1, ay=0.2, az=1.05`
- **THEN** the accelerometer error metric SHALL be `|√(0.1²+0.2²+1.05²) - 1.0|`
- **THEN** the result SHALL be a non-negative scalar value

#### Scenario: Computation on every sample
- **WHEN** a new raw IMU accelerometer sample arrives
- **THEN** the system SHALL compute the accelerometer error metric before any filtering or complementary filter processing
- **THEN** the metric SHALL be pushed to the accel_err short buffer

### Requirement: Accelerometer Error Short Buffer
The monitor SHALL maintain a rolling short buffer of accelerometer error values with configurable size for live variance calculation.
- The short buffer size SHALL be configured via Kconfig (`CONFIG_MONITOR_ACCEL_ERR_SHORT_BUF_SIZE`), default 256.
- The buffer SHALL use a compile-time fixed-size `std::array`.

#### Scenario: Rolling variance update
- **WHEN** a new accelerometer error value is pushed to the short buffer
- **THEN** the system SHALL update rolling sums and rolling sum-of-squares
- **THEN** the rolling variance (`accel_err_var`) SHALL be computed in O(1) time complexity
- **THEN** the computed variance SHALL be available for state transition evaluation

#### Scenario: Buffer not yet full
- **WHEN** the short buffer contains fewer than `CONFIG_MONITOR_ACCEL_ERR_SHORT_BUF_SIZE` samples
- **THEN** the system SHALL compute variance from the available samples
- **THEN** state transitions SHALL NOT occur until the buffer is full

### Requirement: Accelerometer Error Baseline Variance
The monitor SHALL accumulate a baseline accelerometer error variance during IDLE state from the 5-minute storage window.

#### Scenario: Baseline accumulation during IDLE
- **WHEN** the node is in the `IDLE` state
- **WHEN** a 5-minute window completes
- **THEN** the system SHALL compute `accel_err_baseline_var` from the accelerometer error values stored in the 5-minute window
- **THEN** this baseline SHALL be used as the reference for state transition thresholds

#### Scenario: No baseline available yet
- **WHEN** no previous 5-minute window has completed
- **THEN** state transitions based on accel_err_var SHALL NOT occur
- **THEN** the node SHALL remain in `IDLE`

### Requirement: Absolute Minimum Accelerometer Error Variance Floor
The absolute minimum variance floor for accelerometer error (`K_ABS_MIN_ACCEL_VAR`) SHALL be configurable via Kconfig as a scaled integer (`CONFIG_MONITOR_ABS_MIN_ACCEL_VAR_X1000000`).
- The default value SHALL be 100 (representing 0.0001 g²).
- The Kconfig parameter SHALL use x1000000 scaling convention.
- This floor SHALL prevent false transitions when the baseline is near-zero.

#### Scenario: Configuration applied at runtime
- **WHEN** the monitor initializes
- **THEN** the absolute minimum accel error variance floor is computed as `CONFIG_MONITOR_ABS_MIN_ACCEL_VAR_X1000000 / 1000000.0f`
- **THEN** this value SHALL be used in all accel-error state transition threshold comparisons as `max(baseline × K, K_ABS_MIN_ACCEL_VAR)`

