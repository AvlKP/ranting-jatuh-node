# Damping Ratio Analysis — Handoff Notes

Context for the next session to continue work on damping estimation. Reference: explore session ending 2026-06-07, after `fix-fft-active-region` change was proposed.

## Status

- Frequency estimation: proposed change ready (`openspec/changes/fix-fft-active-region/`).
- Damping estimation: deferred. This document captures the analysis so the next session can start the proposal phase directly.

## Problem Statement

The current damping estimation in `components/monitor/monitor.cpp` produces unreliable results across the board. From inspection of `logs/DBG_DUMP_0424_070626.CSV`:

- 7 of 8 snapshots report `zeta = 0.0`.
- 1 snapshot reports `zeta = 4.63` for roll axis, which is physically impossible (a damping ratio above 1.0 means an overdamped system that cannot oscillate, yet that same snapshot reports 7 peaks).
- For longer sustained oscillations (Snapshot 7, 8), damping is also zero.

## Algorithm Currently In Place

Two stages, both in `components/monitor/monitor.cpp`:

1. `FindDecayRegion` (line 540): walks the buffer, finds all local extrema above `peak_min_amplitude_deg` and at least `peak_min_spacing` samples apart, identifies the global maximum, then collects subsequent peaks as long as their amplitude is monotonically declining. Breaks on the first amplitude increase.

2. `ComputeDampingRegression` (line 627): takes the peak list and natural frequency from FFT, performs ordinary least squares regression on `ln(|amp|)` versus time, returns `zeta = |slope| / (2 * pi * fn)`. Requires at least 4 peaks; returns 0 otherwise.

## Root Causes Identified

### RC1: Monotonic-decline assumption is too strict

The peak collection loop breaks at the first amplitude increase. Real ringdown signals always have some noise or beat patterns where one peak appears slightly larger than its predecessor. The loop terminates prematurely, giving only 2-4 peaks even when many real peaks exist.

Example from Snapshot 1 (manual flick, roll axis):
- Sample 254: value 0.768 (global maximum, marked as peak start)
- Sample 257: value 0.136 (trough, abs_val < last_amp, accepted)
- Sample 258: value 0.640 (peak, would break the loop, but is skipped due to `min_spacing=2`)
- Sample 259: value 0.107 (trough, abs_val < last_amp, accepted)
- Sample 260: value 0.277 (peak, abs_val > last_amp 0.107, loop BREAKS)

Result: only 3 peaks captured. The real ringdown that follows (visible in raw data through sample 410) is never seen. Without at least 4 peaks, the regression returns zero.

### RC2: Peaks and troughs are conflated as `abs_val`

`FindDecayRegion` stores `std::fabs(curr)` for both local maxima and local minima. The damping regression then fits a single envelope through both. This works if the signal is symmetric about zero, but if there is any DC offset or asymmetry, the resulting envelope mixes positive peaks with negative troughs at different amplitudes.

### RC3: `min_spacing = 2` aliases real high-frequency peaks

At 26 Hz sampling rate with `min_spacing = 2`, the closest accepted peaks are 0.077 s apart, which is a 13 Hz signal at full alternation. For the user's bench tests (flicks produce 3-10 Hz response), real peaks can fall within the spacing window and get filtered out. This is what happened at sample 258 above.

### RC4: Log-decrement model assumes free decay

The algorithm `zeta = |slope of ln(amp) vs t| / wn` is derived from the assumption that the envelope follows `A(t) = A0 * exp(-zeta * wn * t)`. This holds for free vibration of an SDOF system after the forcing stops. It does NOT hold for forced steady-state response (where amplitude reflects energy balance with the forcing, not damping). It also fails when the signal contains multiple modes with different damping ratios.

## User Test Setup Clarification (important)

Per user feedback in the explore session:

- Current bench setup: manual pull on a small branch held fixed at one end, then release. These tests should produce free decay (so log-decrement is theoretically appropriate). The reason damping still fails is RC1, not RC4.
- User does not drive sustained oscillation in bench tests; envelope decays naturally after release. They observed roughly constant envelopes because the recording captured the active pull phase, not necessarily long after release.
- Production: real branch under wind. Tree under wind is NOT pure forced steady-state. Wind gusts are random, response includes free-decay phases between gusts.
- RDT (Random Decrement Technique) is of interest for production deployment.

## Snapshot 1 Full Trace

This is the most informative snapshot for understanding the algorithm failure. Raw values from `logs/DBG_DUMP_0424_070626.CSV`, roll axis, samples 240-340:

```
Sample range  Approximate values
[240..249]    0.013  0.012  0.013  0.012  0.012  0.012  0.012  0.012  0.012  0.014
[250..259]    0.019  0.083  0.230  0.375  0.768  0.144  0.140  0.136  0.640  0.107
[260..269]    0.277  0.127 -0.020  0.258  0.096  0.304  0.507  0.310  0.534  0.351
[270..279]    0.501  0.648  0.473  0.640  0.548  0.457  0.532  0.392  0.519  0.645
[280..289]    0.558  0.620  0.504  0.590  0.675  0.586  0.663  0.561  0.461  0.534
[290..299]    0.449  0.503  0.439  0.377  0.412  0.367  0.392  0.416  0.377  0.399
[300..309]    0.362  0.382  0.403  0.369  0.386  0.357  0.328  0.341  0.315  0.328
[310..319]    0.340  0.316  0.326  0.304  0.314  0.323  0.304  0.309  0.294  0.279
[320..329]    0.282  0.268  0.270  0.258  0.248  0.249  0.239  0.238  0.238  0.230
[330..339]    0.228  0.221  0.217  0.214  0.209  0.205  0.201  0.198  0.194  0.192
```

Pattern: sharp impact at sample 254 (peak 0.768), then real oscillation 260-340 with envelope decaying from 0.6 to 0.2 over about 80 samples (3.1 s). Peaks appear roughly every 5-8 samples, suggesting a natural frequency around 3-5 Hz at 26 Hz sampling.

The algorithm captures the impact peak and two early troughs, then exits. The 80 samples of useful decay are discarded.

## Damping Methods Considered

| Method | Required Signal | Cost | Notes |
|---|---|---|---|
| Log decrement on peaks (current) | Clean free decay, monotonic envelope | Low | Brittle to noise, fails if envelope wobbles |
| Half-power bandwidth | Steady-state response, good FFT resolution | Free with FFT | `zeta = (f_high - f_low) / (2 * f0)` where f_high, f_low are -3 dB points |
| Hilbert envelope decay | Any signal, fits exponential to envelope | Medium | Robust to noise, gives smooth amplitude estimate |
| Random Decrement Technique (RDT) | Long continuous record under random excitation | Low | Designed for ambient vibration; averages out forcing |
| Autocorrelation envelope (Bendat-Piersol) | Stationary random response | Medium | RDT variant; works well for wind data |
| AR(2) Yule-Walker | Any signal with dominant mode | Very low (~30 FLOPs) | Extracts pole, derives zeta and freq jointly |

## RDT Deep Dive

Random Decrement Technique was the most promising candidate raised. Key points:

### Theory

Under random forcing, the autocorrelation of the system response equals the impulse response of the system (Wiener-Khinchin theorem applied to lightly damped systems). The RDT estimator captures segments triggered on a fixed condition (typically zero crossings or level crossings) and averages them. The random forcing contribution averages to zero across enough segments, leaving the free response.

### Implementation Sketch

```
trigger_level = 2.0 * sigma_signal   # set once per axis based on recent variance
segment_length_samples = 128         # roughly 5 cycles at expected freq
accumulator = float[segment_length_samples] = {0}
segment_count = 0

for each new sample s_now (called every IMU update):
    push s_now into rolling buffer
    if rising crossing of +trigger_level just occurred AND
       previous segment had at least 1.5 * segment_length_samples gap:
        copy next segment_length_samples into temporary buffer
        accumulator[i] += temp[i] for i in 0..segment_length_samples
        segment_count += 1
    if segment_count >= 50:
        averaged = accumulator / segment_count
        compute zeta from averaged using log decrement (now reliable)
        reset accumulator, segment_count
```

### Cost

- RAM: 128 floats accumulator + 128 floats temp buffer + small state = under 2 KB.
- CPU: 1 comparison per sample for trigger detection, plus accumulation when triggered. Under 10 FLOPs per sample amortized.
- Latency: 50 segments at typical wind trigger rate (1 per 5-30 s in moderate wind) means a damping estimate every few minutes to half hour. Acceptable for slow degradation monitoring.

### Applicability To User's Test Cases

- Bench flick test: a single transient gives only 3-5 trigger crossings within one event. Not enough segments for RDT averaging. Use Hilbert envelope or fit to individual peaks instead.
- Hand-driven oscillation: more triggers but still few. Marginal for RDT.
- Production wind: ideal. Continuous data, many independent gusts, thousands of triggers per day.

### Reference

Bendat and Piersol, "Engineering Applications of Correlation and Spectral Analysis" (Wiley). Cole 1968 is the original RDT paper for civil engineering.

## Recommended Path For Next Session

### Phase A: Fix the immediate breakage (small change)

Relax the monotonic envelope requirement in `FindDecayRegion`. Specific changes:

1. Allow amplitude to increase by up to a tolerance factor (e.g. `amp <= 1.2 * last_amp`) without breaking the loop. This lets small noise bumps through while still terminating on real re-excitation.
2. Separate peak list from trough list. Run regression on each independently and average the two zeta estimates, or use whichever has more data points.
3. Lower the minimum peak count from 4 to 3 if separate peak/trough lists are used (effectively doubling the available data per ringdown).
4. Tighten `peak_min_amplitude_deg`. Current default 0.5 deg is reasonable but consider making it adaptive: `max(0.1 deg, 0.2 * max_observed_peak)`.

Expected outcome: bench flick tests start producing nonzero damping values. Need calibration to know what range is reasonable for the test branch.

### Phase B: Add Hilbert envelope path (medium change)

Implement Hilbert transform via FFT (already available) to compute the analytic signal, then fit an exponential to its magnitude. This sidesteps the discrete peak detection entirely and gives a smooth envelope. Useful for bench tests where ringdown is short.

### Phase C: Add RDT path for production (larger change)

Implement RDT as a continuous background process that runs during DISTURBED state (or even IDLE if wind keeps the signal active). Emit damping estimates on a fixed schedule, independent of the per-disturbance event pipeline. This becomes the primary damping output for field deployments.

## Open Questions

- What is the actual expected damping range for the bench test branch? Need calibration data before judging if results are reasonable. User said "don't know yet, need research."
- Should the per-disturbance damping estimate be removed entirely once RDT is in place, or kept as a fast first-look indicator?
- For RDT, what trigger condition gives best segment quality: zero crossings, level crossings at fixed amplitude, level crossings at adaptive amplitude (proportional to running sigma)? Literature favors adaptive level crossings but the implementation cost is higher.
- How does the existing post-hoc decay region (from `posthoc-decay-detection` spec) interact with new envelope-based methods? May need spec rework.

## Code Reference Points

- `components/monitor/monitor.cpp:540` — `FindDecayRegion` (the brittle algorithm).
- `components/monitor/monitor.cpp:627` — `ComputeDampingRegression` (regression, fine as-is).
- `components/monitor/monitor.cpp:431` — `ComputeAndPublish`, calls both above on DISTURBED to IDLE transition.
- `components/monitor/include/monitor.hpp:111` — `PeakList` struct, currently sized 256 max peaks.
- `openspec/specs/posthoc-decay-detection/spec.md` — current requirements; may need MODIFIED delta in the upcoming damping change.
- `openspec/specs/envelope-damping-regression/spec.md` — current regression requirements; the 4-peak minimum is specified here.
- `openspec/specs/free-decay-analysis/spec.md` — to be modified by `fix-fft-active-region` first; further changes for damping will come after.

## Related Existing Specs To Review

- `openspec/specs/posthoc-decay-detection/spec.md`
- `openspec/specs/envelope-damping-regression/spec.md`
- `openspec/specs/free-decay-analysis/spec.md`

These three together govern the damping pipeline. The proposed Phase A change above would modify `posthoc-decay-detection` (relax monotonicity) and `envelope-damping-regression` (separate peak/trough handling, possibly lower min peak count). Phase B and Phase C would likely add new capabilities (`hilbert-envelope-damping`, `rdt-damping-estimation`).
