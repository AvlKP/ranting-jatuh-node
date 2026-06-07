## ADDED Requirements

### Requirement: Raw IMU Recording

The system SHALL provide a separate build target that records raw IMU samples (3-axis accelerometer, 3-axis gyroscope, and on-die temperature) to SD card without any signal processing.

#### Scenario: Logger startup and file creation
- **WHEN** the raw logger binary boots
- **THEN** the system SHALL initialize I2C communication with the LSM6DS3 at 400 kHz
- **THEN** the system SHALL configure the LSM6DS3 at the Kconfig-selected output data rate (26, 52, 104, or 208 Hz) with ±2g accelerometer range and ±250 dps gyroscope range
- **THEN** the system SHALL mount the SD card via SDMMC 1-bit mode
- **THEN** the system SHALL create a new CSV file at `/sdcard/raw_log_<timestamp>.csv`

#### Scenario: Periodic sample acquisition
- **WHEN** the logger task loop reaches its next sampling interval
- **THEN** the system SHALL read raw accelerometer (ax, ay, az in g) and gyroscope (gx, gy, gz in dps) values from the LSM6DS3 output registers
- **THEN** the system SHALL read the on-die temperature from registers OUT_TEMP_L and OUT_TEMP_H and convert to degrees Celsius using the formula `T(°C) = raw_int16 / 256.0 + 25.0`
- **THEN** the system SHALL acquire a microsecond timestamp via `esp_timer_get_time()`
- **THEN** the system SHALL write one CSV row in the format `timestamp_us,ax,ay,az,gx,gy,gz,temp_c` to the open file

#### Scenario: Configured duration completes
- **WHEN** the Kconfig duration `CONFIG_RAW_LOGGER_DURATION_S` is greater than zero
- **WHEN** the elapsed samples equal `duration_s × rate_hz`
- **THEN** the system SHALL flush and close the CSV file
- **THEN** the system SHALL log a completion message and suspend the logging task

#### Scenario: Indefinite recording
- **WHEN** the Kconfig duration `CONFIG_RAW_LOGGER_DURATION_S` is zero
- **THEN** the system SHALL record samples indefinitely until power is removed
- **THEN** the system SHALL NOT close or rotate the file mid-session

#### Scenario: IMU read failure
- **WHEN** an I2C read of accel/gyro or temperature fails
- **THEN** the system SHALL log a warning
- **THEN** the system SHALL skip writing a row for that sample interval
- **THEN** the system SHALL continue the recording loop at the next interval

### Requirement: Build Target Isolation

The raw IMU logger SHALL be a compile-time separate binary that does not link against monitor, logger, dashboard, or MQTT components.

#### Scenario: Kconfig toggle selects logger binary
- **WHEN** `CONFIG_APP_BUILD_RAW_LOGGER` is set to `y`
- **THEN** the build system SHALL compile `raw_logger_main.cpp` instead of `main.cpp` and `verify.cpp`
- **THEN** the build system SHALL link only `lsm6ds3`, `sdmmc`, `fatfs`, `driver`, and `log` components
- **THEN** the resulting firmware image SHALL NOT contain monitor, logger, dashboard, or MQTT code

#### Scenario: Kconfig toggle selects main binary
- **WHEN** `CONFIG_APP_BUILD_RAW_LOGGER` is set to `n`
- **THEN** the build system SHALL compile `main.cpp` and `verify.cpp` as before
- **THEN** all existing components SHALL link and function as before
- **THEN** `raw_logger_main.cpp` SHALL NOT be compiled

### Requirement: Temperature Sensor Access

The LSM6DS3 driver SHALL expose a method to read the on-die temperature sensor.

#### Scenario: Read temperature
- **WHEN** `read_temp(float& out_temp_c)` is called
- **THEN** the driver SHALL read 2 bytes from registers `OUT_TEMP_L` (0x20) and `OUT_TEMP_H` (0x21)
- **THEN** the driver SHALL combine them as a signed 16-bit integer in little-endian order
- **THEN** the driver SHALL convert to degrees Celsius: `out_temp_c = (int16_t)raw / 256.0f + 25.0f`
- **THEN** the method SHALL return `true` on success, `false` on I2C failure

### Requirement: Configurable Logger Parameters

The raw logger SHALL expose configuration via Kconfig under a dedicated menu.

#### Scenario: ODR configuration
- **WHEN** the user sets `CONFIG_RAW_LOGGER_IMU_RATE_HZ`
- **THEN** the LSM6DS3 SHALL be configured at that output data rate
- **THEN** the logger task loop SHALL sample at that rate

#### Scenario: Duration configuration
- **WHEN** the user sets `CONFIG_RAW_LOGGER_DURATION_S` to a positive value
- **THEN** the logger SHALL stop after that many seconds of recording
- **WHEN** the user sets `CONFIG_RAW_LOGGER_DURATION_S` to zero
- **THEN** the logger SHALL record indefinitely

#### Scenario: Build target selection
- **WHEN** `CONFIG_APP_BUILD_RAW_LOGGER` is toggled
- **THEN** the corresponding source files SHALL be selected for compilation
