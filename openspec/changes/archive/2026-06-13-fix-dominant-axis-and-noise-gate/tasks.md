## 1. Kconfig

- [x] 1.1 Add `CONFIG_MONITOR_NOISE_GATE_GMAG_X10` to `components/monitor/Kconfig` with default 80 (8.0 dps), help text describing it as minimum peak gyro magnitude for damping computation
- [x] 1.2 Add `noise_gate_gmag_dps` field to `MonitorConfig` struct in `monitor.hpp`, initialized from `CONFIG_MONITOR_NOISE_GATE_GMAG_X10 / 10.0f`

## 2. Peak gmag tracking

- [x] 2.1 Add `float peak_gmag_{0.0f}` member to Monitor class in `monitor.hpp`
- [x] 2.2 Update `peak_gmag_` in `PushSample()`: when state transitions to DISTURBED, reset to gmag; on each DISTURBED sample, `peak_gmag_ = std::max(peak_gmag_, gmag)`
- [x] 2.3 Reset `peak_gmag_` to 0.0f on DISTURBED->IDLE transition after compute

## 3. Fix dominant axis selection

- [x] 3.1 Rewrite `ComputeDominantAxisSway()` to track cumulative sum AND min/max of cumulative angle per axis in single pass
- [x] 3.2 Update `SwayAxisResult` to store peak-to-peak values: `sx_deg` = max_angle_x - min_angle_x (same for y, z)
- [x] 3.3 Dominant axis selection unchanged: argmax of absolute peak-to-peak values
- [x] 3.4 Update comment/doc on `ComputeDominantAxisSway` to reflect peak-to-peak method

## 4. Noise gate in AnalyzeImuEvent

- [x] 4.1 In `AnalyzeImuEvent()`, after decay onset detection succeeds, check `peak_gmag_ < config_.noise_gate_gmag_dps`
- [x] 4.2 If noise gate triggers: skip damping; compute dominant axis and FFT normally. Return result with computed `natural_freq_hz`, `damping_ratio = 0.0f`, confidence "low"
- [x] 4.3 Add ESP_LOGD message when noise gate triggers

## 5. Tests

- [x] 5.1 Update `test_monitor_algorithms.cpp`: add test for `ComputeDominantAxisSway` with symmetric oscillation (verify peak-to-peak > 0 despite zero cumulative sum)
- [x] 5.2 Add test for `ComputeDominantAxisSway` with asymmetric oscillation (verify dominant axis selection)
- [x] 5.3 Add test for noise gate: verify `AnalyzeImuEvent` returns zero damping when peak_gmag below threshold
- [x] 5.4 Add test for noise gate: verify `AnalyzeImuEvent` computes damping normally when peak_gmag above threshold

## 6. Build and verify

- [x] 6.1 Build firmware (`idf.py build`) — verify no regressions
- [x] 6.2 Run unit tests — tests compile; full run requires ESP32-S3 hardware
- [x] 6.3 Verify Python reference still matches — imports OK, no regression test suite found
