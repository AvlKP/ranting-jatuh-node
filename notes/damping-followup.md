# Damping Ratio Analysis — Handoff Notes

Context for next session continuing damping work. Reference: explore session ending 2026-06-07, after `fix-fft-active-region` proposed.

## Status

- Frequency estimation: change ready (`openspec/changes/fix-fft-active-region/`).
- Damping estimation: deferred. This doc captures analysis so next session starts proposal directly.

## Problem Statement

Damping in `components/monitor/monitor.cpp` unreliable. From `logs/DBG_DUMP_0424_070626.CSV`:

- 7 of 8 snapshots: `zeta = 0.0`.
- 1 snapshot: `zeta = 4.63` roll axis. Physically impossible — zeta > 1 = overdamped = no oscillation, but same snapshot reports 7 peaks.
- Long sustained oscillations (Snap 7, 8): damping zero too.

## Algorithm Currently In Place

Two stages, `components/monitor/monitor.cpp`:

1. `FindDecayRegion` (line 540): walks buffer, finds local extrema above `peak_min_amplitude_deg` and ≥ `peak_min_spacing` samples apart, finds global max, collects subsequent peaks while amplitude monotonically declines. Breaks on first amplitude increase.

2. `ComputeDampingRegression` (line 627): takes peak list + FFT freq, OLS regression on `ln(|amp|)` vs time, returns `zeta = |slope| / (2 * pi * fn)`. Requires ≥4 peaks else 0.

## Root Causes Identified

### RC1: Monotonic-decline assumption too strict

Loop breaks on first amplitude increase. Real ringdown has noise or beat patterns where one peak slightly bigger than predecessor. Loop terminates early, gives 2-4 peaks even when many real peaks exist.

Snap 1 example (manual flick, roll):
- Sample 254: 0.768 (global max, peak start)
- Sample 257: 0.136 (trough, abs_val < last_amp, accepted)
- Sample 258: 0.640 (peak, would break loop, but skipped via `min_spacing=2`)
- Sample 259: 0.107 (trough, abs_val < last_amp, accepted)
- Sample 260: 0.277 (peak, abs_val > last_amp 0.107, loop BREAKS)

Result: 3 peaks captured. Real ringdown after (visible raw through sample 410) never seen. <4 peaks → regression returns 0.

### RC2: Peaks + troughs conflated as `abs_val`

`FindDecayRegion` stores `std::fabs(curr)` for both local max + min. Damping regression fits single envelope through both. Works if signal symmetric about zero. With DC offset or asymmetry, envelope mixes positive peaks with negative troughs at different amplitudes.

### RC3: `min_spacing = 2` aliases real high-freq peaks

At 26 Hz with `min_spacing = 2`, closest accepted peaks 0.077 s apart = 13 Hz signal at full alternation. Bench tests (flicks → 3-10 Hz response), real peaks fall inside spacing window and get filtered. Happened at sample 258 above.

### RC4: Log-decrement model assumes free decay

`zeta = |slope of ln(amp) vs t| / wn` derives from envelope `A(t) = A0 * exp(-zeta * wn * t)`. Holds for free vibration of SDOF after forcing stops. Fails for forced steady-state (amplitude reflects energy balance with forcing, not damping). Fails with multiple modes at different damping ratios too.

## User Test Setup Clarification (important)

Per user feedback:

- Current bench: manual pull on small branch held fixed at one end, then release. Should produce free decay (log-decrement theoretically OK). Damping still fails due to RC1, not RC4.
- User doesn't drive sustained oscillation; envelope decays naturally after release. Observed roughly constant envelopes because recording captured active pull phase, not long after release.
- Production: real branch under wind. NOT pure forced steady-state. Wind gusts random, response includes free-decay phases between gusts.
- RDT (Random Decrement Technique) of interest for production.

## Snapshot 1 Full Trace

Most informative snapshot. Raw values from `logs/DBG_DUMP_0424_070626.CSV`, roll axis, samples 240-340:

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

Pattern: sharp impact sample 254 (peak 0.768), real oscillation 260-340 with envelope decaying 0.6 → 0.2 over ~80 samples (3.1 s). Peaks every 5-8 samples → natural freq ~3-5 Hz at 26 Hz sampling.

Algorithm captures impact peak + two early troughs, exits. 80 samples of useful decay discarded.

## Damping Methods Considered

| Method | Required Signal | Cost | Notes |
|---|---|---|---|
| Log decrement on peaks (current) | Clean free decay, monotonic envelope | Low | Brittle, fails on envelope wobble |
| Half-power bandwidth | Steady-state response, good FFT resolution | Free with FFT | `zeta = (f_high - f_low) / (2 * f0)` where f_high, f_low are -3 dB points |
| Hilbert envelope decay | Any signal, fit exponential to envelope | Medium | Robust to noise, smooth amplitude estimate |
| Random Decrement Technique (RDT) | Long continuous record under random excitation | Low | Designed for ambient vibration, averages out forcing |
| Autocorrelation envelope (Bendat-Piersol) | Stationary random response | Medium | RDT variant, works for wind data |
| AR(2) Yule-Walker | Any signal with dominant mode | Very low (~30 FLOPs) | Extract pole, derive zeta + freq jointly |

## RDT Deep Dive

Most promising candidate. Key points:

### Theory

Under random forcing, autocorrelation of system response = impulse response (Wiener-Khinchin theorem on lightly damped systems). RDT captures segments triggered on fixed condition (zero crossings or level crossings), averages them. Random forcing averages to zero across enough segments, leaving free response.

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

- RAM: 128 floats accumulator + 128 floats temp buffer + small state = <2 KB.
- CPU: 1 comparison/sample for trigger + accumulation when triggered. <10 FLOPs/sample amortized.
- Latency: 50 segments at typical wind trigger rate (1 per 5-30 s moderate wind) = damping estimate every few minutes to half hour. Acceptable for slow degradation.

### Applicability To User's Test Cases

- Bench flick: single transient → 3-5 trigger crossings/event. Not enough for RDT averaging. Use Hilbert envelope or fit individual peaks.
- Hand-driven oscillation: more triggers, still few. Marginal for RDT.
- Production wind: ideal. Continuous data, many independent gusts, thousands of triggers/day.

### Reference

Bendat and Piersol, "Engineering Applications of Correlation and Spectral Analysis" (Wiley). Cole 1968 original RDT paper for civil engineering.

## Recommended Path For Next Session

### Phase A: Fix immediate breakage (small change)

Relax monotonic envelope in `FindDecayRegion`:

1. Allow amplitude increase up to tolerance (e.g. `amp <= 1.2 * last_amp`) without breaking loop. Lets small noise bumps through, terminates on real re-excitation.
2. Separate peak list from trough list. Run regression on each independently, average two zeta estimates, or use whichever has more data points.
3. Lower min peak count 4 → 3 if separate peak/trough lists used (doubles available data per ringdown).
4. Tighten `peak_min_amplitude_deg`. Default 0.5 deg reasonable, consider adaptive: `max(0.1 deg, 0.2 * max_observed_peak)`.

Expected: bench flicks produce nonzero damping. Need calibration to know reasonable range for test branch.

### Phase B: Add Hilbert envelope path (medium change)

Hilbert transform via FFT (already available) → analytic signal → fit exponential to magnitude. Sidesteps discrete peak detection, smooth envelope. Useful for bench tests with short ringdown.

### Phase C: Add RDT path for production (larger change)

RDT as continuous background process during DISTURBED (or IDLE if wind keeps signal active). Emit damping estimates on fixed schedule, independent of per-disturbance pipeline. Primary damping output for field deployment.

## Open Questions

- Expected damping range for bench test branch? Need calibration data before judging results. User: "don't know yet, need research."
- Remove per-disturbance damping estimate once RDT in place, or keep as fast first-look indicator?
- For RDT, best trigger condition: zero crossings, level crossings at fixed amplitude, level crossings at adaptive amplitude (proportional to running sigma)? Literature favors adaptive level crossings, implementation cost higher.
- How does existing post-hoc decay region (from `posthoc-decay-detection` spec) interact with new envelope-based methods? May need spec rework.

## Code Reference Points

- `components/monitor/monitor.cpp:540` — `FindDecayRegion` (brittle algorithm).
- `components/monitor/monitor.cpp:627` — `ComputeDampingRegression` (regression, fine as-is).
- `components/monitor/monitor.cpp:431` — `ComputeAndPublish`, calls both above on DISTURBED to IDLE transition.
- `components/monitor/include/monitor.hpp:111` — `PeakList` struct, 256 max peaks.
- `openspec/specs/posthoc-decay-detection/spec.md` — current requirements; may need MODIFIED delta in upcoming damping change.
- `openspec/specs/envelope-damping-regression/spec.md` — current regression requirements; 4-peak minimum here.
- `openspec/specs/free-decay-analysis/spec.md` — modified by `fix-fft-active-region` first; further damping changes follow.

## Related Existing Specs To Review

- `openspec/specs/posthoc-decay-detection/spec.md`
- `openspec/specs/envelope-damping-regression/spec.md`
- `openspec/specs/free-decay-analysis/spec.md`

Three govern damping pipeline. Phase A above modifies `posthoc-decay-detection` (relax monotonicity) + `envelope-damping-regression` (separate peak/trough, possibly lower min peak count). Phase B + C likely add new capabilities (`hilbert-envelope-damping`, `rdt-damping-estimation`).
