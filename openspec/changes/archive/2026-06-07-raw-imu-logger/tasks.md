## 1. Kconfig — Build target and logger parameters

- [x] 1.1 Add `APP_BUILD_RAW_LOGGER` bool to `main/Kconfig` (default n, help text explains it builds a separate raw IMU logger binary instead of the monitoring app)
- [x] 1.2 Add new menu "Raw IMU Logger" to `main/Kconfig`, gated on `CONFIG_APP_BUILD_RAW_LOGGER` (visible only when logger mode is selected)
- [x] 1.3 Add `CONFIG_RAW_LOGGER_IMU_RATE_HZ` int (default 26, range 26 208, help text lists valid ODR values matching LSM6DS3)
- [x] 1.4 Add `CONFIG_RAW_LOGGER_DURATION_S` int (default 0, range 0 86400, help text: 0 = indefinite until power cycle, otherwise stop after N seconds)
- [x] 1.5 Run `idf.py reconfigure` with logger disabled (n) and enabled (y) to verify Kconfig parses correctly and generates the expected defines

## 2. CMakeLists.txt — Conditional compilation

- [x] 2.1 Update `main/CMakeLists.txt`: wrap `SRCS` in `if(CONFIG_APP_BUILD_RAW_LOGGER)`/`else()`/`endif()` to select `raw_logger_main.cpp` vs `main.cpp` + `verify.cpp`
- [x] 2.2 Update `REQUIRES` list in the same conditional: logger binary only needs `lsm6ds3 sdmmc fatfs driver log` (no monitor, logger, dashboard, esp_event)
- [x] 2.3 Run `idf.py build` with logger disabled → main binary compiles clean; run with logger enabled → logger binary compiles clean

## 3. LSM6DS3 driver — Temperature reading

- [x] 3.1 Add `bool read_temp(float& out_temp_c)` declaration to `components/lsm6ds3/include/lsm6ds3.hpp` (public method)
- [x] 3.2 Implement `read_temp` in `components/lsm6ds3/lsm6ds3.cpp`: read 2 bytes from `OUT_TEMP_L` (0x20) and `OUT_TEMP_H` (0x21), combine as `int16_t` LE, convert to `°C = raw / 256.0f + 25.0f`
- [x] 3.3 Verify temperature reading compiles in the main binary (the driver change is shared, so both binaries must build)

## 4. Raw logger binary — raw_logger_main.cpp

- [x] 4.1 Create `main/raw_logger_main.cpp` with minimal includes: `pins.hpp`, `lsm6ds3.hpp`, `sdmmc_cmd.h`, `esp_vfs_fat.h`, `esp_timer.h`, `esp_log.h`, FreeRTOS headers
- [x] 4.2 Duplicate I2C init helper (`InitImuI2c`) from `main.cpp` into the logger file (per design decision D2: duplicated, not abstracted)
- [x] 4.3 Duplicate IMU ODR mapping helper (`MapImuOdr`) into the logger file, wired to `CONFIG_RAW_LOGGER_IMU_RATE_HZ`
- [x] 4.4 Duplicate SD card mount helper (`InitSdCard`) into the logger file — only the SDMMC 1-bit case needed (per existing project default), guarded by `#if CONFIG_APP_SD_INTERFACE_SDMMC`
- [x] 4.5 Implement standalone `app_main()`: call init helpers in order (I2C → IMU → SD card), open CSV file, enter FreeRTOS task loop
- [x] 4.6 Implement task loop: `vTaskDelayUntil` at period = 1000/rate_hz ms, call `read_accel_gyro()`, call `read_temp()`, `fprintf` one CSV row per sample
- [x] 4.7 Implement duration control: count samples, if `CONFIG_RAW_LOGGER_DURATION_S > 0`, stop after `duration_s * rate_hz` samples, flush and close file
- [x] 4.8 Generate output filename from RTC time via `time()` (if RTC is set from previous main-app NTP sync) with fallback to `raw_log_<uptime_seconds>.csv`
- [x] 4.9 Add `ESP_LOGI` messages at key milestones: startup (rate, duration, file path), periodic heartbeat (every N seconds), shutdown complete

## 5. Build and verification

- [x] 5.1 `idf.py build` with `CONFIG_APP_BUILD_RAW_LOGGER=n` — confirm main binary builds clean and all existing functionality is intact
- [x] 5.2 `idf.py build` with `CONFIG_APP_BUILD_RAW_LOGGER=y` — confirm logger binary builds clean with no link errors or unused-symbol warnings
- [x] 5.3 `idf.py -p <port> flash monitor` with logger binary — verify boot messages, IMU init success, SD mount success, CSV file created, sample rows appearing
- [x] 5.4 Let the logger run for 10 seconds, power cycle, extract SD card, verify CSV file is parseable with Python `pandas.read_csv()`, check column count (8) and value sanity (accel magnitudes near 1g, temperature between 20-40°C)
