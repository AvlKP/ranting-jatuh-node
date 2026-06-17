## Why

The enclosure lid is flat and suitable for PCB mounting, while the body requires a hole for the power connector. Mounting the PCB on the lid requires rotating it 180° around the LSM6DS3 Y-axis, which inverts the sensor X and Z axes relative to the branch frame. Without a software correction layer, pitch/roll estimation produces garbage and gyro integration runs in the wrong direction.

## What Changes

- Add a Kconfig bool `CONFIG_IMU_MOUNT_LID` (default `n`) to select between body-mount (original) and lid-mount (180° Y-flip) orientations.
- Insert an axis transform step in `monitor.cpp` after bias subtraction: negate accel and gyro X/Z when `CONFIG_IMU_MOUNT_LID` is enabled.
- Update unit tests to cover the lid-mount transform path.
- Calibration biases are sensor-intrinsic and subtracted in sensor frame before the rotation — no bias changes needed.

## Capabilities

### New Capabilities
- `imu-mount-transform`: Compile-time selectable axis transform that maps sensor-frame IMU data to branch-frame data based on PCB mounting orientation.

### Modified Capabilities

None. The transform is inserted between bias subtraction and the existing filter pipeline. Downstream specs (adaptive-complementary-filter, imu-calibration, imu-event-analysis) are unaffected because they receive branch-frame data regardless of mount orientation.

## Impact

- **`components/monitor/monitor.cpp`**: New transform block after bias subtraction (lines ~127-142), gated by `CONFIG_IMU_MOUNT_LID`.
- **`main/Kconfig.projbuild`** (or component Kconfig): New bool config option.
- **`components/monitor/test/test_monitor_algorithms.cpp`**: New test cases verifying axis negation for lid-mount configuration.
- **No changes to**: filter code, modal analyzer, disturbance detector, calibration storage, logger, dashboard, LSM6DS3 driver.
