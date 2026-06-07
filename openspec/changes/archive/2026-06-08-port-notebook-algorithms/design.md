## Context

Current production monitor uses `|accel_mag - 1.0|` variance for disturbance detection (NOTES.md Decision 3/6) and fixed-alpha complementary filter (Decision 1). Notebook experiments (NOTES.md Decisions 7, 8, 9, 10) validated superior alternatives against real branch data. All four improvements are O(1) memory and suitable for ESP32-S3 fixed-point later.

Existing code paths affected: `monitor.cpp:PushSample()` (lines 294-429), `complementary_filter.hpp` (entire class), `monitor.hpp` member variables (lines 211-221), `Kconfig` (lines 160-186).

## Goals / Non-Goals

**Goals:**
- Replace `|accel_mag-1.0|` variance detector with per-axis Chebyshev Type 1 HPF + magnitude threshold
- Replace fixed-alpha `filter::Complementary` with adaptive complementary filter
- Add static IMU bias subtraction before filter pipeline
- Fix axis convention in angle formulas (pitch/roll → correct branch frame)
- Preserve existing FSM semantics (2-state, exit debounce, buffer refresh)
- Preserve existing taring, modal analysis, failure detection untouched

**Non-Goals:**
- No quaternion/Mahony filter (rejected in Decision 8)
- No online FFT or continuous modal analysis
- No fixed-point conversion (float OK for now, Q31 left as future)
- No dashboard/logger changes

## Decisions

### D1: Chebyshev HPF — Direct Form II Biquad Per Axis

**Choice:** Three independent 2nd-order Direct Form II biquads (X, Y, Z), coefficients designed offline for nominal fs=26 Hz, fc=0.1 Hz, ripple=1.0 dB.

**Rationale:** Direct Form II uses 2 state variables per axis (w1, w2) vs Direct Form I's 4. Total state = 18 floats (w1/w2 × 3 axes) + 5 coefficients (b0,b1,b2,a1,a2). No history buffer needed. Per-sample: 9 multiplies + 9 adds. Equivalent to Python `notebook/disturbance_detection.py:cheby1_hpf_apply()`.

**Alternatives:** Direct Form I (more states, same output), fixed-coefficient at build time via constexpr (adds complexity without benefit yet), single biquad on magnitude (loses per-axis info, equivalent to scalar HPF).

**Coefficients:** Pre-computed offline with `scipy.signal.cheby1(N=2, rp=1.0, Wn=0.1, btype='high', fs=26.0)`, normalized to a0=1, hardcoded as `constexpr float` in header.

### D2: Detection Threshold — Single HPF Magnitude

**Choice:** `sqrt(hpf_x² + hpf_y² + hpf_z²) > HPF_THRESHOLD_G` (default 0.02 g). Replace all accel_err_baseline, K_HIGH/K_LOW, abs_min_var logic.

**Rationale:** HPF removes DC gravity, so threshold is absolute (not relative to baseline). Simpler: no baseline accumulation, no KRatio multipliers. Same 64-sample exit debounce preserved. Settle period (500 samples) added for HPF transient convergence — matches filter settle in taring.

**FSM semantics preserved:** IDLE→DISTURBED on HPF magnitude > threshold (after settle). DISTURBED→IDLE on HPF magnitude < threshold for 64 consecutive samples.

### D3: Adaptive Complementary Filter — Continuous Alpha

**Choice:** `alpha = 1.0 - (1.0 - alpha_base) * (1.0 / (1.0 + k_gain * |accel_mag - 1.0|))`. New class `filter::AdaptiveComplementary` with same interface as `filter::Complementary`.

**Rationale:** When `|accel_mag-1.0|` is near zero, alpha ≈ alpha_base (0.98). During disturbance (large accel error), alpha → 1.0 (trust gyro only). Prevents transient accel corruption of orientation. Cost: +3 multiplies, +1 divide per sample vs fixed-alpha. State: 4 floats (pitch_rad, roll_rad + 2 prev accel for edge case).

**Alternatives:** Mahony (overkill — Decision 8), switched alpha (discrete, loses smooth transition).

### D4: Axis Convention — Correct Branch Frame

**Choice:** pitch = `atan2(ax, az)` driven by gyro[1], roll = `atan2(-ay, az)` driven by gyro[0].

**Rationale:** Branch physical axes: z-up (vertical), x-down (along branch away from trunk), y-toward-joint. Old formulas: `pitch=atan2(ay, sqrt(ax²+az²))` (correct for NED drone, wrong for branch). New formulas validated in notebook Decision 10. Applied to both `Complementary` and `AdaptiveComplementary`.

**Gyro mapping:** gx→roll (rotation about x = twist), gy→pitch (rotation about y = sag). Old code had gy→pitch correct but with wrong accel formula; gx→roll also correct but with wrong accel formula.

### D5: IMU Calibration — NVS Storage, Apply in Update()

**Choice:** Static biases stored as 6 floats in NVS (`calib` namespace, key `imu_bias`). Subtracted from raw accel/gyro values in `Monitor::Update()` before filter_.update(). If NVS key missing, biases default to zero (no calibration).

**Rationale:** Biases are sensor-specific, measured once during production (NOTES.md Decision 9 provides reference values from test unit). NVS persists across reboots. Application before filter ensures all downstream processing uses calibrated data. Not applied in `raw_logger_main.cpp` (raw logger should record uncalibrated).

**Kconfig:** `CONFIG_MONITOR_IMU_CALIBRATION` bool (default y). When disabled, biases set to zero without NVS read.

## Risks / Trade-offs

- **HPF coefficient mismatch at non-26 Hz ODR** → Accept. ESP32 uses fixed 26 Hz rate. If rate changes, regenerate coefficients.
- **HPF transient at startup** → Mitigated by 500-sample settle period (reuses existing tare settle counter).
- **Adaptive filter uses `|accel_mag-1.0|` for alpha** → Same metric being removed from disturbance detection. But here it only affects filter weight (orientation quality), not state transitions. False positives in alpha have no downstream effect beyond slightly noisier orientation.
- **NVS wear from calibration writes** → Write-once during production provisioning. No runtime writes.
- **Axis fix changes angle sign/range** → Taring re-computes offsets, so no persistent offset issue. Dashboard labels unchanged (roll/pitch semantics preserved).
