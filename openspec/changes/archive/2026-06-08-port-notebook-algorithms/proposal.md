## Why

Notebook analysis (NOTES.md) refined four algorithms after testing against real branch pull-and-release data. The current ESP32 production code uses older, less accurate methods: variance-based `|accel_mag-1.0|` disturbance detection (fails when forces are parallel to gravity), fixed-alpha complementary filter (drifts during transients), no IMU bias calibration, and cross-wired axis formulas. Port the validated notebook algorithms to production to improve detection accuracy and orientation stability.

## What Changes

- **Replace variance-based disturbance detection with Chebyshev Type 1 HPF** — 2nd-order biquad per axis, HPF magnitude vs threshold, existing FSM preserved
- **Replace fixed-alpha complementary filter with adaptive complementary** — variable alpha suppresses accel weight during disturbances
- **Add IMU calibration** — subtract static accel/gyro biases before filter processing, biases stored in NVS
- **Fix axis convention** — correct pitch=atan2(ax,az)/gyro[1], roll=atan2(-ay,az)/gyro[0] (z-up, x-down branch frame)
- **BREAKING**: Removes `accel_err_baseline_var_`, `K_HIGH`, `K_LOW`, `ABS_MIN_ACCEL_VAR` from disturbance detection (replaced by HPF threshold)
- **BREAKING**: Removes `MONITOR_ACCEL_ERR_SHORT_BUF_SIZE`, `MONITOR_K_HIGH_X100`, `MONITOR_K_LOW_X100`, `MONITOR_ABS_MIN_ACCEL_VAR_X1000000` Kconfig entries

## Capabilities

### New Capabilities
- `imu-calibration`: Static bias subtraction for accel and gyro before filter processing, biases stored in NVS
- `chebyshev-hpf-disturbance`: 2nd-order Chebyshev Type 1 HPF per-axis disturbance detection replacing variance-based accel error method
- `adaptive-complementary-filter`: Variable-alpha complementary filter that suppresses accel weight during high-acceleration transients

### Modified Capabilities
- `accel-error-state-detection`: **REPLACED** — variance-based `|accel_mag-1.0|` metric removed; replaced by Chebyshev HPF magnitude vs threshold
- `node-state-machine`: **UPDATED** — transition triggers changed from `accel_err_var` comparisons to HPF magnitude threshold; baseline mechanism removed; settle period added for HPF convergence at startup; debounce and two-threshold FSM semantics preserved

## Impact

- **components/filter/** — new `adaptive_complementary_filter.hpp`, modify `complementary_filter.hpp` (axis fix)
- **components/monitor/** — `monitor.cpp` PushSample() disturbance detection rewritten, `monitor.hpp` member cleanup, `Kconfig` accel_err entries removed + HPF/calibration entries added
- **components/logger/** — NVS storage for calibration biases (new `logger_nvs.cpp` or inline in monitor init)
- **components/monitor/test/** — new/disturbance detection tests for Chebyshev HPF
- **main/main.cpp** — no changes required (filter replacement is internal to monitor)
