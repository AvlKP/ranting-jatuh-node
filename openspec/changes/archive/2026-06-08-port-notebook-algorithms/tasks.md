## 1. Kconfig and MonitorConfig Changes

- [x] 1.1 Remove deprecated Kconfig entries: `CONFIG_MONITOR_K_HIGH_X100`, `CONFIG_MONITOR_K_LOW_X100`, `CONFIG_MONITOR_ABS_MIN_ACCEL_VAR_X1000000`, `CONFIG_MONITOR_ACCEL_ERR_SHORT_BUF_SIZE`
- [x] 1.2 Add new Kconfig entries in `components/monitor/Kconfig`: `CONFIG_MONITOR_HPF_THRESHOLD_X1000` (int, default 20, range 1-1000), `CONFIG_MONITOR_HPF_SETTLE_SAMPLES` (int, default 500, range 0-5000), `CONFIG_MONITOR_IMU_CALIBRATION` (bool, default y)
- [x] 1.3 Add filter config fields to `MonitorConfig` struct in `monitor.hpp`: `filter_alpha_base` (default 0.98f), `filter_k_gain` (default 50.0f)

## 2. Axis Convention Fix in Complementary Filter

- [x] 2.1 Fix pitch formula in `filter::Complementary::update()`: change `atan2(accel[1], sqrt(accel[0]²+accel[2]²))` to `atan2(accel[0], accel[2])`
- [x] 2.2 Fix roll formula in `filter::Complementary::update()`: change `atan2(-accel[0], accel[2])` to `atan2(-accel[1], accel[2])`

## 3. Adaptive Complementary Filter

- [x] 3.1 Create `components/filter/include/adaptive_complementary_filter.hpp` with class `filter::AdaptiveComplementary` using corrected axis formulas, variable alpha computation (`alpha_base`, `k_gain`), same public interface as `Complementary` (`update()`, `pitch()`, `roll()`)
- [x] 3.2 Replace `filter::Complementary` with `filter::AdaptiveComplementary` as member type in `monitor.hpp`, update `#include` directive
- [x] 3.3 Update `Monitor::Monitor()` constructor to pass `config.filter_alpha_base` and `config.filter_k_gain` to `AdaptiveComplementary` constructor

## 4. IMU Calibration

- [x] 4.1 Create `components/monitor/include/calibration.hpp` with `CalibrationBias` struct (6 floats: ax, ay, az, gx, gy, gz) and `Calibration` namespace with `ReadBiases(nvs_handle, bias)` and `WriteBiases(nvs_handle, bias)` functions
- [x] 4.2 Add `calibration::CalibrationBias calib_bias_{}` member and NVS handle to `Monitor` class in `monitor.hpp`
- [x] 4.3 In `Monitor::Init()`, open NVS namespace `calib`, read biases via `Calibration::ReadBiases()` when `CONFIG_MONITOR_IMU_CALIBRATION` is enabled
- [x] 4.4 In `Monitor::Update()`, subtract biases from raw `accel.x/y/z` and `gyro.x/y/z` before calling `filter_.update()`, store raw (uncalibrated) values in `StreamSample`

## 5. Chebyshev HPF Disturbance Detection

- [x] 5.1 Create `components/monitor/include/chebyshev_hpf.hpp` with `ChebyshevHpf` class containing: Direct Form II biquad state (w1, w2 per axis = 6 floats), hardcoded `constexpr` coefficients (b0, b1, b2, a1, a2) for 0.1 Hz cutoff at 26 Hz, `update(ax, ay, az)` method returning `hpf_magnitude`
- [x] 5.2 Add `ChebyshevHpf hpf_` member and `float hpf_settle_counter_` to `Monitor` class in `monitor.hpp`
- [x] 5.3 Remove deprecated accel_err members from `monitor.hpp`: `accel_err_short_`, `accel_err_short_sum_`, `accel_err_short_sq_sum_`, `accel_err_short_write_index_`, `accel_err_short_sample_count_`, `accel_err_baseline_var_`, `has_accel_err_baseline_`, `baseline_accum_sum_`, `baseline_accum_sq_sum_`, `baseline_sample_count_`
- [x] 5.4 Remove `static constexpr std::size_t kAccelErrShortBufferSamples` from `monitor.hpp`
- [x] 5.5 Rewrite `Monitor::PushSample()` signature to accept `float hpf_magnitude` instead of raw accel, or call `hpf_.update()` inside PushSample
- [x] 5.6 Replace FSM transition logic in `PushSample()`: use `hpf_magnitude > hpf_threshold_g` for IDLE→DISTURBED (no baseline, immediate entry), `hpf_magnitude < hpf_threshold_g` with debounce for DISTURBED→IDLE, add settle counter gate at startup
- [x] 5.7 Update `Monitor::Update()` to call `hpf_.update()` with calibrated accel values and pass `hpf_magnitude` to `PushSample()`
- [x] 5.8 Remove accel_err baseline computation from 5-minute IDLE window logic in `Update()`

## 6. Unit Tests

- [x] 6.1 Add test for adaptive complementary filter: verify alpha approaches 1.0 during high accel error, alpha approaches alpha_base during normal conditions, angles continuous
- [x] 6.2 Add test for Chebyshev HPF biquad: verify DC rejection (constant input → zero output), verify coefficients loaded correctly
- [x] 6.3 Add test for axis convention fix: verify pitch and roll signs for known branch orientations
- [x] 6.4 Add test for calibration bias subtraction: verify biases applied correctly, zero-default when NVS key missing
- [x] 6.5 Update existing monitor FSM tests if any reference accel_err_var or K_HIGH/K_LOW

## 7. Integration and Verification

- [x] 7.1 Run `idf.py build` to verify compilation with new Kconfig defaults
- [x] 7.2 Verify `idf.py build` succeeds with `CONFIG_MONITOR_IMU_CALIBRATION=n`
- [x] 7.3 Verify existing `test_monitor_modal.cpp` tests still pass (no regression in modal analysis)
- [x] 7.4 Run all unit tests via `idf.py build && idf.py flash monitor` test target
