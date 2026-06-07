## Context

The notebook currently uses calibrated IMU data, adaptive complementary pitch/roll, Chebyshev HPF disturbance detection, and FFT/damping helpers in `notebook/natural_frequency.py`. Release data around 96 s in `logs/raw_log_7.csv` contains dynamic acceleration and a slow pitch baseline relaxation. De-meaning a full event does not remove that exponential-like centerline motion.

## Goals / Non-Goals

**Goals:**
- Reuse current logs by limiting FFT search to 0.5-12 Hz.
- Make pitch damping insensitive to baseline offset by using half peak-to-peak amplitudes.
- Keep the algorithm translatable to ESP32-S3: fixed arrays, O(N) passes, no nonlinear optimizer.
- Preserve old metrics for side-by-side comparison.

**Non-Goals:**
- No production C++ implementation in this change.
- No full damped-sinusoid nonlinear fit.
- No change to raw logger ODR, although later production work can use 52 Hz.

## Decisions

### D1: Centerline from alternating extrema

Detect local maxima/minima with existing spacing rules, then pair adjacent opposite-sign extrema. Keep peak-pair amplitudes above `centerline_min_amp=0.05 deg` by default, independent from the sway threshold. For each pair, compute:

```
center_time = (t0 + t1) / 2
center_value = (v0 + v1) / 2
amplitude = abs(v1 - v0) / 2
```

Interpolate center values over the signal and subtract to produce residual tilt. Use amplitudes for damping regression.

Rationale: half peak-to-peak amplitude is invariant to a slowly moving baseline. A lower centerline threshold keeps late decay cycles available for damping without changing sway metrics. It is also cheaper and more deterministic than fitting an exponential plus sinusoid.

### D2: Bounded FFT peak search

FFT and Welch PSD remain unchanged except peak selection only considers bins between `freq_min_hz` and `freq_max_hz`. Defaults are 0.5 Hz and 12 Hz.

Rationale: low-frequency baseline decay cannot win the FFT, and 12 Hz is below Nyquist for both nominal 26 Hz data and the observed current log sample rate.

### D3: HPF cutoff becomes 0.2 Hz for notebook disturbance detection

The Chebyshev Type 1 HPF remains a second-order per-axis biquad; only default cutoff changes from 0.1 Hz to 0.2 Hz.

Rationale: stronger low-frequency rejection improves event segmentation while staying below expected natural frequencies.

## Risks / Trade-offs

- Missing or noisy extrema can produce too few peak pairs -> return zero damping and keep diagnostics visible.
- If natural frequency is close to 0.5 Hz, the lower search bound can bias FFT upward -> keep bound explicit in outputs.
- Centerline interpolation is an approximation, not a physical baseline model -> acceptable for notebook experiment before firmware work.
