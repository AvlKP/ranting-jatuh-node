## 1. Kconfig

- [x] 1.1 Add `CONFIG_MONITOR_IMU_MOUNT_LID` bool to `components/monitor/Kconfig` under the Monitor menu, default `n`, with help text describing the 180° Y-axis rotation for lid-mount PCB orientation.

## 2. Axis Transform

- [x] 2.1 In `Monitor::Update()` (`components/monitor/monitor.cpp`), add a `#if CONFIG_MONITOR_IMU_MOUNT_LID` block after bias subtraction (lines 134-139) that negates `calib_ax`, `calib_az`, `calib_gx`, and `calib_gz` before they are used to construct `accel_vec`/`gyro_vec`.

## 3. Tests

- [x] 3.1 Add a unit test in `components/monitor/test/test_monitor_algorithms.cpp` verifying that lid-mount transform negates X and Z for accelerometer data while preserving Y.
- [x] 3.2 Add a unit test verifying that lid-mount transform negates X and Z for gyroscope data while preserving Y.
- [x] 3.3 Add a unit test verifying that a vertical branch with lid-mount (sensor reads `ax=0, ay=0, az=-1.0`) produces near-zero pitch and roll after transform.

## 4. Verification

- [x] 4.1 Build with `CONFIG_MONITOR_IMU_MOUNT_LID=n` (default) and confirm no changes to binary behavior.
- [x] 4.2 Build with `CONFIG_MONITOR_IMU_MOUNT_LID=y` and confirm compilation succeeds.
- [x] 4.3 Run unit tests for both configurations.
