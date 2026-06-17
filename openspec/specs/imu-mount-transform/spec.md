## ADDED Requirements

### Requirement: Compile-time mount orientation selection
The monitor component SHALL provide a Kconfig bool `CONFIG_MONITOR_IMU_MOUNT_LID` (default disabled) that selects between body-mount (original, no transform) and lid-mount (180° Y-axis rotation) PCB orientations.

#### Scenario: Kconfig option exists with correct default
- **WHEN** `menuconfig` is opened under the Monitor menu
- **THEN** an option "IMU lid-mount orientation (180 deg Y-flip)" SHALL be visible
- **THEN** it SHALL default to disabled (body-mount)

### Requirement: Axis transform for lid-mount orientation
When `CONFIG_MONITOR_IMU_MOUNT_LID` is enabled, the monitor SHALL negate the X and Z components of both accelerometer and gyroscope data after bias subtraction and before feeding to the orientation filter and all downstream processing.

#### Scenario: Lid-mount enabled, static vertical branch
- **WHEN** `CONFIG_MONITOR_IMU_MOUNT_LID` is enabled
- **AND** the sensor reads `ax_sensor, ay_sensor, az_sensor` after bias subtraction
- **THEN** the values passed to the filter SHALL be `(-ax_sensor, ay_sensor, -az_sensor)`

#### Scenario: Lid-mount enabled, gyroscope transform
- **WHEN** `CONFIG_MONITOR_IMU_MOUNT_LID` is enabled
- **AND** the sensor reads `gx_sensor, gy_sensor, gz_sensor` after bias subtraction
- **THEN** the values passed to the filter SHALL be `(-gx_sensor, gy_sensor, -gz_sensor)`

#### Scenario: Lid-mount disabled, no transform applied
- **WHEN** `CONFIG_MONITOR_IMU_MOUNT_LID` is disabled
- **THEN** calibrated sensor values SHALL pass through to the filter unchanged
- **AND** the transform code SHALL be eliminated at compile time (zero overhead)

### Requirement: Transform applied after bias subtraction
The axis transform SHALL be applied after sensor-frame bias subtraction and before any orientation filter update, disturbance detection input, or sample storage.

#### Scenario: Pipeline ordering with calibration enabled
- **WHEN** `CONFIG_MONITOR_IMU_CALIBRATION` and `CONFIG_MONITOR_IMU_MOUNT_LID` are both enabled
- **THEN** the processing order SHALL be: raw read → bias subtraction → axis transform → filter update

#### Scenario: Pipeline ordering with calibration disabled
- **WHEN** `CONFIG_MONITOR_IMU_CALIBRATION` is disabled and `CONFIG_MONITOR_IMU_MOUNT_LID` is enabled
- **THEN** the processing order SHALL be: raw read → axis transform → filter update
- **AND** biases SHALL be zero (no subtraction effect), but the transform SHALL still apply

### Requirement: All downstream consumers receive branch-frame data
After the axis transform, all consumers of IMU data (filter, disturbance detector, modal analyzer, sample store, log output) SHALL receive branch-frame values regardless of mount orientation.

#### Scenario: Disturbance detector receives transformed data
- **WHEN** `CONFIG_MONITOR_IMU_MOUNT_LID` is enabled
- **THEN** the gyro magnitude `sqrt(gx^2 + gy^2 + gz^2)` computed from transformed values SHALL equal the magnitude computed from untransformed values (rotation preserves magnitude)

#### Scenario: Roll and pitch are correct for lid-mount
- **WHEN** `CONFIG_MONITOR_IMU_MOUNT_LID` is enabled
- **AND** the branch is hanging vertically (gravity along branch Z-up axis)
- **THEN** `filter_.pitch()` SHALL be approximately zero
- **AND** `filter_.roll()` SHALL be approximately zero
