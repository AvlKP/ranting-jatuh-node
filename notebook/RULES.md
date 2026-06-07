# Rules — Ranting Jatuh Node Notebook

## Communication
1. Gaps → ask. No guess. No implicit assumptions.
2. Think system-level before code. Data flow, architecture, contracts first.
3. Edits surgical. Minimal diff. No collateral changes.

## Code Structure
4. Algorithms → separate `.py` in `notebook/`. Notebook imports them. Keeps ipynb clean.
5. Production target: ESP32-S3 (FreeRTOS, C++, no-heap post-init). Algorithm design starts from MCU constraints — fixed-size buffers, O(1), no dynamic alloc. Python replicas are analysis tools only.

## Architecture Tracking
6. Architectural decisions → log reasoning in `notebook/NOTES.md`.
7. All changes aggregated there.

## Production: Orient. Filter
- Adaptive complementary (alpha_base=0.98, k_gain=50.0). Alpha continuously increases toward 1.0 as accel magnitude error grows.
- Disturbance: Chebyshev Type 1 HPF (order=2, ripple=1.0 dB, fc=0.1 Hz) on accel per-axis. O(1) biquad.
- Calibration: static bias from NVS. Accel + gyro biases subtracted pre-processing.
- Axis convention: z=up (crown), x=down, y=toward-joint. Pitch=rot about y (sag), Roll=rot about x (twist).

## Production: Metrics
- Taring: settle 500 samples, average next 100. Offset subtracted from angle.
- Sway: peak-to-peak max/mean via extrema detection (min_amp=0.1 deg, min_spacing=2).
- NatFreq: Welch PSD (1024-pt, Hanning, 50% overlap). Short signals padded to 512/1024.
- Damping: log-decay regression on declining envelope peaks. Zeta = |slope|/(2*pi*fn). Min 4 peaks.
