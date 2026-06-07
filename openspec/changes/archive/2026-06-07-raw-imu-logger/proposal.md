## Why

The firmware debug loop is too slow for algorithm iteration. Changing C++ code, building, flashing, physically disturbing a branch, extracting the SD card, and analyzing takes minutes to hours per cycle. The user needs to record raw IMU data once and replay it through a Python replica of the pipeline for instant feedback on parameter changes.

## What Changes

- Add a separate build target (Kconfig-selectable) that compiles a raw IMU logger binary instead of the main monitoring application.
- The logger binary initializes I2C, IMU at configurable ODR, and SD card, then writes one CSV row per sample: `timestamp_us, accel_x, accel_y, accel_z, gyro_x, gyro_y, gyro_z, temp_c`.
- Add `read_temp()` method to the LSM6DS3 driver to expose the on-die temperature sensor.
- Add Kconfig options: `APP_BUILD_RAW_LOGGER` (bool to select binary), `RAW_LOGGER_IMU_RATE_HZ` (ODR), `RAW_LOGGER_DURATION_S` (run time, 0 = infinite until power cycle).
- The main monitoring binary is **untouched** — this is a compile-time choice, not a runtime mode.
- No processing: no complementary filter, no FSM, no FFT, no MQTT, no dashboard.

## Capabilities

### New Capabilities
- `raw-imu-recording`: Captures raw IMU samples (accel, gyro, temperature) to SD card at a configurable output data rate for offline algorithm development.

### Modified Capabilities
<!-- None — separate binary, main pipeline unchanged -->

## Impact

- **New file**: `main/raw_logger_main.cpp` — standalone app_main for the logger binary.
- **Modified**: `main/CMakeLists.txt` — conditional `SRCS` based on `CONFIG_APP_BUILD_RAW_LOGGER`.
- **Modified**: `main/Kconfig` — new menu entry for build target selection + logger parameters.
- **Modified**: `components/lsm6ds3/lsm6ds3.cpp` + `lsm6ds3.hpp` — add `read_temp(float& out_temp_c)` method.
- **Reuses**: existing I2C init, IMU config, SD card mount helpers from `main/main.cpp` (extracted or duplicated).
- **No changes** to monitor, logger, dashboard, MQTT, or any production pipeline code.
