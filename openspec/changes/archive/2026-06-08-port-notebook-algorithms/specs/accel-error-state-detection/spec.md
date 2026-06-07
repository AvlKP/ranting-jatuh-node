## REMOVED Requirements

### Requirement: Accelerometer Error Metric Computation
**Reason**: Replaced by Chebyshev Type 1 HPF per-axis filtering. The `|accel_mag-1.0|` scalar metric fails to detect disturbances where lateral forces are parallel to gravity, as documented in NOTES.md Decision 6/7.
**Migration**: Use `chebyshev-hpf-disturbance` spec. Accelerometer data now passes through per-axis Direct Form II biquad HPF; the HPF magnitude `sqrt(hpf_x²+hpf_y²+hpf_z²)` replaces `|accel_mag-1.0|` as the detection signal.

### Requirement: Accelerometer Error Short Buffer
**Reason**: The HPF method is per-sample (no variance window needed). The biquad maintains only 2 state variables per axis (O(1) memory), eliminating the 256-sample short buffer entirely.
**Migration**: Remove `accel_err_short_` buffer and associated rolling sum/sum-of-squares variables from `monitor.hpp`. The HPF biquad states replace the short buffer.

### Requirement: Accelerometer Error Baseline Variance
**Reason**: HPF threshold is absolute (0.02g, not relative to a baseline). No baseline accumulation needed during IDLE.
**Migration**: Remove `accel_err_baseline_var_`, `has_accel_err_baseline_`, `baseline_accum_sum_`, `baseline_accum_sq_sum_`, `baseline_sample_count_` members from monitor. The 5-minute IDLE window still computes roll/pitch variance for statistics.

### Requirement: Absolute Minimum Accelerometer Error Variance Floor
**Reason**: HPF uses single absolute threshold (0.02g). No variance floor protection needed.
**Migration**: Remove `CONFIG_MONITOR_ABS_MIN_ACCEL_VAR_X1000000` from Kconfig. The HPF threshold `CONFIG_MONITOR_HPF_THRESHOLD_X1000` serves as the sole detection threshold.

### Requirement: Two-Threshold Accel Error Detection
**Reason**: Replaced by single-threshold HPF magnitude detection with exit debounce. Entry is immediate (no K_HIGH multiplier), exit requires debounce (no K_LOW multiplier).
**Migration**: Remove `CONFIG_MONITOR_K_HIGH_X100` and `CONFIG_MONITOR_K_LOW_X100` from Kconfig. Use `CONFIG_MONITOR_HPF_THRESHOLD_X1000` for both entry and exit. `CONFIG_MONITOR_DISTURBED_EXIT_DEBOUNCE` is preserved.
