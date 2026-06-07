## Context

The main application (`main/main.cpp`) wires together the full monitoring pipeline: I2C, IMU, complementary filter, FSM, FFT, damping regression, MQTT logger, and dashboard. This binary is unsuitable for raw data capture because it processes, transforms, and discards the raw IMU samples.

For offline algorithm development, the user needs unprocessed IMU data — the same 3-axis accel + gyro + temperature values that the LSM6DS3 outputs — written directly to SD card.

The LSM6DS3 driver already reads accel and gyro via `read_accel_gyro()`. It does not expose temperature. The temperature register pair `OUT_TEMP_L` (0x20) and `OUT_TEMP_H` (0x21) is documented in the datasheet but unused.

The SD card mount code and I2C initialization in `main/main.cpp` are specific to the app entry point. The raw logger needs the same hardware setup but none of the application logic.

## Goals / Non-Goals

**Goals:**
- Provide a separate build target that compiles a raw IMU logger binary with zero processing overhead.
- Log raw accel, gyro, and temperature at configurable ODR (26, 52, 104, 208 Hz) to SD card.
- Write in simple CSV format: one row per sample, columns: `timestamp_us, accel_x, accel_y, accel_z, gyro_x, gyro_y, gyro_z, temp_c`.
- Optional run duration (0 = indefinite until power cycle).
- Keep main application binary and all its components untouched.

**Non-Goals:**
- Runtime mode switching between logger and monitor. This is a compile-time choice.
- Complementary filter, FSM, FFT, damping, MQTT, dashboard, or any processing.
- Python analysis script (out of scope — user writes that themselves with recorded data).
- Streaming over serial or Wi-Fi.
- Deep sleep or power optimization.
- Writing to NVS or any persistent config.

## Decisions

### D1: Build target selection via Kconfig + CMakeLists.txt conditional

**Decision:** Add `CONFIG_APP_BUILD_RAW_LOGGER` (bool, default n) to `main/Kconfig`. In `main/CMakeLists.txt`, conditionally set `SRCS`:

```
if(CONFIG_APP_BUILD_RAW_LOGGER)
    set(raw_logger_srcs "raw_logger_main.cpp")
else()
    set(raw_logger_srcs "verify.cpp" "main.cpp")
endif()
idf_component_register(SRCS ${raw_logger_srcs} ...)
```

**Rationale:** No preprocessor noise in either file. The unused file is simply not compiled. ESP-IDF Kconfig ensures mutually exclusive configuration — you can't accidentally build both. One `idf.py build` after `idf.py menuconfig` toggle.

**Alternatives considered:**
- Separate ESP-IDF project: duplicates all component code. Violates DRY. Requires syncing changes to LSM6DS3 driver, pins, etc.
- `#ifdef` gating: would contaminate `main.cpp` with conditional blocks. Harder to read and maintain.
- Runtime flag in NVS: adds startup complexity, requires NVS partition, doesn't save RAM since both code paths link.

### D2: Code reuse strategy — duplicate with caveat

**Decision:** `raw_logger_main.cpp` duplicates the I2C init, IMU config, ODR mapping, and SD card mount helper functions from `main.cpp` (~150 lines). Both files are short and the shared hardware init is unlikely to change independently.

**Rationale:** Extracting shared init into a separate component adds an abstraction layer for code that is ~150 lines and changes rarely. The maintenance risk (keeping two copies in sync) is lower than the abstraction cost (new component, new API, new include paths, new mental overhead). If the init logic diverges significantly, extraction can be done later.

**Alternatives considered:**
- Extract to `components/hardware_init/`: over-engineered for 150 lines. Each file has slightly different needs (logger doesn't need event loop, dashboard, MQTT).
- Keep both `main.cpp` and `raw_logger_main.cpp` in SRCS, gate via `#ifdef` on `app_main()`: confusing. Two `app_main` symbols don't link.

### D3: CSV format

**Decision:** One row per sample. No header row. No snapshot delimiters. File named `raw_log_YYYYMMDD_HHMMSS.csv` based on RTC time at start (fallback: uptime-based counter).

```
<timestamp_us>,<ax>,<ay>,<az>,<gx>,<gy>,<gz>,<temp_c>
```

- `timestamp_us`: `esp_timer_get_time()` value at sample acquisition (microseconds since boot).
- `ax/ay/az`: float, accel in g (post-sensitivity conversion, same as `read_accel_gyro` output).
- `gx/gy/gz`: float, gyro in dps.
- `temp_c`: float, temperature in degrees Celsius. Formula: `raw_temp / 256.0f + 25.0f`.

**Rationale:** No header simplifies Python `pandas.read_csv(names=[...])` or `np.loadtxt()`. No delimiters keeps the file appendable if power cycles (timestamp epoch reset is handled by having a separate file per session). Floating-point precision: `%.6f` matches the existing debug dump format.

**File location:** `CONFIG_APP_SD_MOUNT_POINT /raw_log_<timestamp>.csv`.

### D4: Temperature reading — add to LSM6DS3 driver

**Decision:** Add `bool read_temp(float& out_temp_c)` to `sensor::Lsm6ds3`. Reads `OUT_TEMP_L` (0x20) and `OUT_TEMP_H` (0x21), converts via standard ST MEMS formula: `T(°C) = ((int16_t)raw) / 256.0f + 25.0f`.

**Rationale:** The register definition already exists in `lsm6ds3_detail.hpp` (`OUT_TEMP_L`, `OUT_TEMP_H`). Adding a method to the driver is the natural home — any future code that needs temperature (e.g., calibration compensation) reuses the same path. Reading temperature adds one I2C transaction (2 bytes) per sample, which is acceptable at ≤208 Hz with a 400 kHz I2C bus.

**Alternatives considered:**
- Read temperature registers directly in `raw_logger_main.cpp`: bypasses the driver abstraction. Inconsistent with how accel/gyro are read.
- Skip temperature: user asked for it specifically. Useful for correlating IMU drift with thermal conditions.

### D5: ODR selection

**Decision:** Reuse the existing ODR enum mapping from `main.cpp` (`MapImuOdr` function). The logger Kconfig provides `CONFIG_RAW_LOGGER_IMU_RATE_HZ` with options matching the LSM6DS3 supported rates: 26, 52, 104, 208 Hz (default 26 Hz, matching main app).

**Rationale:** Same sensor, same driver config, same valid ODR values. The mapping function is copied into `raw_logger_main.cpp` (per D2). Note that temperature output rate follows the accel ODR — the LSM6DS3 updates temperature at the same rate as the accelerometer.

### D6: Task loop structure

**Decision:** A stripped-down FreeRTOS task loop. No mutex, no event groups, no FSM. The loop body:

```
vTaskDelayUntil(&last_wake, period_ticks);
ReadImu(gyro, accel);
ReadTemp(temp_c);
fprintf(f, "%llu,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
        esp_timer_get_time(), accel.x, accel.y, accel.z,
        gyro.x, gyro.y, gyro.z, temp_c);
```

**Rationale:** `fprintf` + SD card write per sample is the naive approach. At 26 Hz with ~90 bytes/row, the data rate is ~2.3 KB/s — well within SD card write bandwidth. At 208 Hz it's ~18.7 KB/s — still fine. If SD latency causes timing jitter, the `vTaskDelayUntil` still maintains the long-term average rate. A buffered approach (accumulate N rows, write once) could be added later but is premature optimization.

**Duration control:** If `CONFIG_RAW_LOGGER_DURATION_S > 0`, count samples and stop after `duration_s * rate_hz`. If 0, run forever until power cycle. File is flushed and closed on exit.

### D7: Dependencies

**Decision:** The raw logger binary links only the components it actually uses: `lsm6ds3`, `sdmmc`, `fatfs`, `driver`, `log`, `nvs_flash`. It does NOT link `monitor`, `logger`, `dashboard`, `esp_event`.

**Rationale:** Smaller binary, faster build, no accidental dependency on processing code. `nvs_flash` is needed for the boot counter (D9). The CMakeLists.txt `REQUIRES` list is conditional in the same `if(CONFIG_APP_BUILD_RAW_LOGGER)` block.

## Risks / Trade-offs

### R1: SD card write latency causing sample jitter
**Risk:** `fprintf` to FATFS may occasionally block for SD card internal housekeeping (wear leveling, erase), causing a sample to be delayed.
**Mitigation:** `vTaskDelayUntil` corrects the long-term phase. A delayed sample shifts the next sample closer in time, but the average rate is maintained. At ≤208 Hz, the worst-case FATFS block (~50-100ms) causes ~1-20 lost samples which is acceptable for offline analysis (NaN gap in CSV). If this becomes problematic, add a ring-buffer and write from a separate low-priority task.

### R2: Temperature reading adds 2 bytes per sample to I2C traffic
**Risk:** Three I2C transactions per sample (12 bytes accel/gyro + 2 bytes temp) instead of two (12 bytes accel/gyro only). At 208 Hz with 400 kHz I2C, the bus utilization increases from ~50% to ~58%.
**Mitigation:** Well within bus capacity. The temperature registers are contiguous with the gyro registers (0x20-0x21 directly before 0x22-0x2D for gyro+accel), so a future optimization could read all 14 bytes in a single burst transaction. Not needed for initial implementation.

### R3: File size for long recordings
**Risk:** At 208 Hz, 90 bytes/row = ~18.7 KB/s = ~67 MB/hour. SD card capacity (typical 8-32 GB) is not a practical constraint. But FATFS has a 4 GB file size limit.
**Mitigation:** At 208 Hz, 4 GB is reached in ~60 hours. For a test recording this is irrelevant. If truly long recordings are needed, a file rotation scheme can be added later.

### D8: FATFS long filename support required
**Decision:** `CONFIG_FATFS_LFN_HEAP=y` is required. The raw logger filenames (`raw_log_<n>.csv` with base name up to 9+ characters, or `raw_log_YYYYMMDD_HHMMSS.csv` with 19+ characters) exceed the 8.3 filename limit. FATFS returns `FR_INVALID_NAME` when LFN is disabled.

**Rationale:** The main app coincidentally fits 8.3 limits (`dbg_dump.csv`, `verify.txt`, `YYYYMMDD.csv`). The logger's descriptive naming requires LFN. `FATFS_LFN_HEAP` costs ~1KB heap for an internal working buffer, which is negligible. The config change is global (shared sdkconfig), not binary-specific — both binaries benefit from future-proofing.

### D9: NVS boot counter for unique filenames without NTP
**Decision:** When RTC is not synchronized (cold boot, no prior NTP sync), use an incrementing boot counter stored in NVS instead of `esp_timer_get_time()` for the filename suffix.

**Rationale:** On every cold boot, `esp_timer_get_time() / 1000000` is 0, producing `raw_log_0.csv` → `fopen(..., "w")` truncates any prior recording. NVS boot counter guarantees unique filenames across power cycles (`raw_log_1.csv`, `raw_log_2.csv`, ...) with zero-network overhead. Adds `nvs_flash` component (~3KB flash). Counter stored in NVS namespace `rawlog`, key `boot_count`.

**When RTC IS valid** (main app previously synced NTP, warm reset), the timestamp filename path is used as before: `raw_log_YYYYMMDD_HHMMSS.csv`.

### R4: No time sync
**Risk:** The logger doesn't connect to Wi-Fi or sync NTP. Timestamps are microseconds since boot, not wall clock.
**Mitigation:** The user knows when they started the test. They can align the boot timestamp to real time manually. For offline analysis, relative time is sufficient — the important thing is sample-to-sample spacing, which `esp_timer_get_time()` provides with microsecond precision. Filename collisions on repeated cold boots are prevented by the NVS boot counter (D9).

## Migration Plan

No migration. This is a new binary, not a change to an existing one. Users toggle `CONFIG_APP_BUILD_RAW_LOGGER=y` in menuconfig, build, flash, record, then toggle back to `n` for normal operation.

Rollback: set `CONFIG_APP_BUILD_RAW_LOGGER=n`, rebuild. No persistent changes.

### R5: Data loss on power cycle in indefinite mode
**Risk:** In indefinite mode (`duration_s=0`), the C stdio buffer (~4KB) holds unwritten data, and FATFS holds additional cached sectors. Power cycle without proper media sync loses buffered samples. The file may also appear empty/corrupt without a proper `fclose`.
**Mitigation:** `std::fflush(f)` + `fsync(fileno(f))` is called every `kHeartbeatSamples` (260 samples = 10 seconds at 26 Hz). `fflush` pushes the C library buffer to the VFS/FATFS layer, and `fsync` calls FATFS `f_sync` to commit all cached sectors to the physical SD card. At most ~10 seconds of data is lost per power cycle. For timed recordings, `fflush` + `fsync` + `fclose` after the loop provides a clean final commit.

## Resolved Questions

1. **Should the file be flushed after every write?** Resolved: periodic flush at every heartbeat (260 samples) is sufficient. ~10s data loss ceiling is acceptable for offline analysis. Per-sample flush would add I/O overhead without meaningful benefit.
2. **Should the LED be used to indicate recording state?** Declined. Not needed for current use case.
3. **Should gyro and accel sensitivity be hardcoded to match main app?** Confirmed: uses LSM6DS3 defaults (`RANGE_2G`, `DPS_250`), matching main app.
