# Development Notes — Branch Monitoring Analysis

## Design Principle
Production implementation runs on ESP32-S3 (FreeRTOS, C++, no heap after init). Algorithm design starts from embedded constraints: fixed-point preferred over float, O(1) memory, no dynamic allocation. Python replicas in notebook are analysis tools — architecture decisions driven by MCU feasibility, not notebook convenience.

## Architecture Decisions

### 1. Complementary Filter — Python Replica
**Date:** 2026-06-07
**Source:** `components/filter/include/complementary_filter.hpp`
**Decision:** 1:1 replica of ESP32 `filter::Complementary` in `notebook/complementary_filter.py`.
- `alpha = 0.98` — biases gyro integration (98%) over accelerometer angles (2%).
- Angles computed via `atan2`: pitch = `atan2(ay, sqrt(ax²+az²))`, roll = `atan2(-ax, az)` — NED convention.
- Gyro z-axis (yaw) ignored — non-observable without magnetometer.
- dt from actual timestamp deltas (not constant 1/26 Hz) — handles FreeRTOS jitter in raw logger output.
- State carried between samples via `pitch_rad`/`roll_rad` return values — matches C++ implicit state.

### 2. Taring Algorithm
**Date:** 2026-06-07
**Source:** `components/monitor/monitor.cpp:176-208`
**Config:** `CONFIG_MONITOR_TARE_SETTLE_SAMPLES=500`, `CONFIG_MONITOR_TARE_SAMPLES=100`
**Decision:** Two-phase calibration.
1. **Settle phase** (500 samples) — discard filter transient while IIR converges.
2. **Tare phase** (100 samples) — accumulate filtered roll/pitch, compute mean as offset.
3. Offset subtracted from all subsequent samples.
- NOT online — computed from complete dataset after full filter pass.
- ESP32 retroactively applies offset to history/stream buffers; notebook skips retroactive since dataset is processed in-order.

### 3. Disturbance Detection FSM
**Date:** 2026-06-07
**Source:** `components/monitor/monitor.cpp:294-429`
**Config:** `SHORT_BUF_SIZE=256`, `K_HIGH=1.50`, `K_LOW=1.10`, `ABS_MIN_VAR=0.0001`, `DEBOUNCE=64`, `BASELINE_WINDOW=1560`
**Status:** REPLACED — see Decision 6.

### 4. Log File Format
**Date:** 2026-06-07
**Source:** `main/raw_logger_main.cpp:244`
**Format:** No header CSV — `timestamp_us,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z,temp_c`
- Accel in g, gyro in dps, temp in °C.
- Sample rate: `CONFIG_RAW_LOGGER_IMU_RATE_HZ=26` (nominal), actual ~33 Hz in test data from FreeRTOS jitter.

### 5. Algorithm-to-File Separation
**Date:** 2026-06-07
**Decision:** Algorithms live in standalone `.py` files under `notebook/`, imported by the notebook. Keeps notebook clean and implementation inspectable.

### 6. Disturbance Detection — IIR HPF
**Date:** 2026-06-07
**Replaces:** Decision 3 (variance-based accel error FSM)
**Superseded by:** Decision 7 (Chebyshev filter upgrade)
**Reasoning:**
- Accel error `|accel_mag - 1.0|` fails when node is not at branch edge — lateral forces parallel to gravity don't change magnitude significantly.
- A disturbance can excite any individual axis independently.
- HPF per-axis removes DC gravity component, leaving only dynamic acceleration.

### 7. Disturbance Detection — Chebyshev Type 1 HPF
**Date:** 2026-06-07
**Replaces:** Decision 6 (first-order IIR HPF)
**Reasoning:**
- First-order IIR has excessively slow rolloff at 0.1 Hz — DC rejection insufficient for branch dynamics.
- Chebyshev Type 1 provides sharper transition band, better DC rejection for same cutoff.
- 2nd order + 1.0 dB ripple chosen as sweet spot: one biquad section, 2 state variables per axis.
**Decision:** 2nd-order Chebyshev Type 1 HPF designed at fixed sample rate (median of actual timestamps). Applied per-axis via Direct Form II biquad.
- **Order:** 2, **Ripple:** 1.0 dB, **fc:** 0.1 Hz.
- **Design:** `scipy.signal.cheby1()` — coefficient design at design fs. Output normalized to a0=1.
- **Apply:** Direct Form II biquad per axis: `w[n]=x[n]-a1*w[n-1]-a2*w[n-2]`, `y[n]=b0*w[n]+b1*w[n-1]+b2*w[n-2]`.
- **Memory:** O(1) — 4 state floats per axis (w1, w2) × 3 axes = 12 floats total. No history buffer.
- **Magnitude:** `sqrt(hpf_x² + hpf_y² + hpf_z²)` vs single threshold (0.02 g).
- **Same FSM:** 64-sample exit debounce preserved. No entry debounce.
- **ESP32 ready:** 3 parallel 2nd-order biquads = 9 multiplies + 9 adds per sample. Coefficients pre-computed offline; fixed-point possible with Q31.

### 8. Orientation Filter Selection — Adaptive Complementary
**Date:** 2026-06-07
**Decision:** Adaptive complementary filter selected for production over basic complementary and Mahony.
**Compared:** Basic complementary (alpha=0.98 fixed), adaptive complementary (alpha varies with accel error), adaptive Mahony (quaternion, PI correction with variable gains).
**Reasoning:**
- Basic complementary drifts during pull/release jumps — gyro integration carries transient error forward.
- Adaptive complementary suppresses accel weight during disturbances (`alpha → 1.0`), preventing jump-induced drift. Adds ~2 multiply-accumulates per sample vs basic.
- Mahony offers theoretical advantage (bias estimation, 3-axis gz) but at cost: quaternion ops + PI controller = ~25 multiplies/sample vs adaptive's ~5. Marginal gain for 2-axis branch monitoring (gz unused, bias handled by tare).
- **ESP32 cost:** Adaptive complementary: 5 multiplies + 3 adds per sample. State = 4 floats (pitch_rad, roll_rad, x_prev, y_prev). No quaternion, no bias estimation.
- **Config:** `alpha_base=0.98`, `k_gain=50.0` — weight drops to ~0.5 at 0.14g error, ~0.1 at 0.42g error. Alpha changes continuously with accel magnitude error.

### 9. IMU Calibration
**Date:** 2026-06-07
**Decision:** Static bias calibration applied before all processing. Biases stored in NVS during production.
- **Accel:** ax=+0.014925, ay=−0.010015, az=+0.010312 (offset from 1g gravity)
- **Gyro:** gx=+1.096412, gy=−2.593744, gz=+0.414028 (dps)
- **Module:** `notebook/calibration.py` — subtracts biases, recomputes magnitudes.

### 10. Axis Convention Fix
**Date:** 2026-06-07
**Fix:** Accel angle formulas corrected for branch physical axes (z-up, x-down, y-toward-joint).
- **Pitch** (branch sag, rotation about y): `atan2(ax, az)`, driven by `gyro[1]`
- **Roll** (branch twist, rotation about x): `atan2(-ay, az)`, driven by `gyro[0]`
- Previous formulas were cross-wired — `atan2(-ax, az)` (pitch formula) fused with roll gyro, `atan2(ay, sqrt(ax²+az²))` (roll formula) fused with pitch gyro.

### 11. Notebook Modal Analysis — Centerline Detrending
**Date:** 2026-06-07
**Change:** `try-centerline-modal-analysis`
**Decision:** Pull-and-release notebook analysis estimates a moving tilt centerline from adjacent peak/trough pairs and subtracts it before FFT. Damping uses half peak-to-peak amplitudes instead of absolute extrema from zero.
- **Disturbance HPF:** Chebyshev Type 1 cutoff is 0.2 Hz for notebook workflow.
- **FFT search:** Default notebook search band is 0.5-12.0 Hz to reuse current 26-33 Hz logs.
- **Centerline threshold:** Half peak-to-peak damping uses `centerline_min_amp=0.05°`, independent from sway threshold.
- **Production note:** Future production ODR can move to 52 Hz, but this notebook experiment does not change firmware.
- **ESP32 feasibility:** Centerline method is O(N), fixed-buffer friendly, and avoids nonlinear fitting.

### 12. Lobe-Collapsed Extrema for Centerline and Damping
**Date:** 2026-06-07
**Change:** `collapse-extrema-by-lobe`
**Decision:** Raw local extrema (3-point detection) are collapsed into one representative per physical peak/trough lobe before centerline pairing. Noisy intra-lobe reversals are gated by `lobe_reversal_min_amp_deg` (defaults to `centerline_min_amp=0.05°`).
- **Algorithm:** Single O(N) forward pass over raw extrema. Retain strongest maximum per peak lobe, strongest minimum per trough lobe. Opposite-kind candidates only flip the active lobe when |diff| ≥ threshold; smaller reversals treated as ripple.
- **Centerline + damping:** Built from collapsed extrema pairs. Raw extrema preserved alongside collapsed for diagnostic comparison.
- **ESP32 feasibility:** Fixed arrays, single forward pass, threshold comparisons — no nonlinear fit, no dynamic allocation.

## Files Created
- `notebook/complementary_filter.py` — basic + adaptive complementary filter + taring
- `notebook/adaptive_complementary_filter.py` — adaptive complementary filter
- `notebook/mahony_filter.py` — adaptive Mahony filter
- `notebook/disturbance_detection.py` — Chebyshev HPF + FSM
- `notebook/calibration.py` — IMU bias calibration
- `notebook/natural_frequency.py` — natural frequency, extrema detection, centerline modal analysis, lobe collapse, damping
- `notebook/analysis.ipynb` — workflow (calibrate → load → 3-filter compare → tare → disturb)
- `notebook/NOTES.md` — this file
