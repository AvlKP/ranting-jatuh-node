## ADDED Requirements

### Requirement: IMU Bias Subtraction
The monitor SHALL subtract static accelerometer and gyroscope biases from raw IMU samples before feeding them to the orientation filter, when calibration is enabled.

#### Scenario: Calibration enabled, biases loaded from NVS
- **WHEN** `CONFIG_MONITOR_IMU_CALIBRATION` is enabled
- **WHEN** NVS contains valid calibration biases under namespace `calib`, key `imu_bias`
- **THEN** each raw accel sample SHALL have `(ax_bias, ay_bias, az_bias)` subtracted before filter processing
- **THEN** each raw gyro sample SHALL have `(gx_bias, gy_bias, gz_bias)` subtracted before filter processing

#### Scenario: Calibration enabled, no biases in NVS
- **WHEN** `CONFIG_MONITOR_IMU_CALIBRATION` is enabled
- **WHEN** NVS does NOT contain key `imu_bias` in namespace `calib`
- **THEN** all biases SHALL default to zero
- **THEN** raw IMU values SHALL pass through unchanged

#### Scenario: Calibration disabled
- **WHEN** `CONFIG_MONITOR_IMU_CALIBRATION` is disabled
- **THEN** no NVS read SHALL be attempted
- **THEN** all biases SHALL be zero
- **THEN** raw IMU values SHALL pass through unchanged

### Requirement: Calibration Bias Storage Format
Calibration biases SHALL be stored as 6 consecutive `float` values in NVS: `[accel_x, accel_y, accel_z, gyro_x, gyro_y, gyro_z]`, accessible via a calibration helper.

#### Scenario: Write biases to NVS
- **WHEN** calibration biases are written via `Calibration::WriteBiases(nvs_handle, biases)`
- **THEN** exactly 24 bytes (6 × sizeof(float)) SHALL be written to NVS key `imu_bias`
- **THEN** the write SHALL use `esp_err_t` return for error propagation

#### Scenario: Read biases from NVS
- **WHEN** calibration biases are read via `Calibration::ReadBiases(nvs_handle, biases)`
- **THEN** the 6-float struct SHALL be populated from NVS key `imu_bias`
- **THEN** if the key does not exist, all 6 fields SHALL remain zero

### Requirement: Calibration Applied Before Filter
Bias subtraction SHALL occur after IMU read and before filter update in the processing pipeline.

#### Scenario: Processing pipeline order
- **WHEN** `Monitor::Update()` reads a new IMU sample
- **THEN** biases SHALL be subtracted from `accel.x/y/z` and `gyro.x/y/z` in-place or into local copies
- **THEN** calibrated values SHALL be passed to `filter_.update()`
- **THEN** raw (uncalibrated) values SHALL be stored in `StreamSample` for dashboard display
