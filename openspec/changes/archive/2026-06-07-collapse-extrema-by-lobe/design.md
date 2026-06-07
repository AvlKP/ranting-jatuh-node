## Context

`notebook/natural_frequency.py` currently detects every 3-point local maximum/minimum, then pairs adjacent opposite extrema for centerline interpolation and half peak-to-peak damping amplitudes. Real pull-and-release pitch data can contain small slope reversals within one physical peak or trough lobe. Those reversals create extra extrema and can make the computed centerline and damping envelope jagged.

This remains a notebook-first experiment. The algorithm should stay simple enough to translate later to ESP32-S3 firmware: fixed arrays, single forward passes, threshold comparisons, and no nonlinear fit.

## Goals / Non-Goals

**Goals:**
- Collapse noisy local extrema into one representative extrema per physical lobe/half-cycle.
- Select the strongest maximum for each peak lobe and strongest minimum for each trough lobe.
- Feed collapsed extrema into centerline pair construction and half peak-to-peak damping regression.
- Keep raw extrema diagnostics visible so tuning mistakes are inspectable.
- Preserve current bounded FFT behavior and notebook-only scope.

**Non-Goals:**
- No production C++ implementation in this change.
- No exponential baseline fit or damped-sinusoid nonlinear fit.
- No SciPy peak-prominence dependency for core extrema selection.
- No change to MQTT payloads, stored logs, server behavior, or firmware sampling.

## Decisions

### D1: Collapse extrema with reversal-confirmed lobes

Run the existing local extrema detector to produce raw candidates. Then make one O(N) pass over candidates:

1. Keep an active lobe kind (`peak` or `trough`) and its strongest candidate.
2. If the next candidate has the same kind, update the active candidate when it is stronger (`max(value)` for peak, `min(value)` for trough).
3. If the next candidate has the opposite kind, only finalize the active lobe and switch kind when the absolute difference from the active strongest candidate is at least `lobe_reversal_min_amp_deg`.
4. If the reversal is too small, treat it as intra-lobe ripple and continue without finalizing.

Default `lobe_reversal_min_amp_deg` should reuse `centerline_min_amp` unless an explicit parameter is provided. That keeps tuning small and makes the lobe gate use the same physical scale as the minimum accepted half peak-to-peak amplitude.

Rationale: noisy ripples near a peak often produce alternating tiny troughs and peaks. A reversal threshold prevents those ripples from becoming real half-cycle transitions, while still selecting one strongest extrema once a real opposite lobe appears.

### D2: Pair collapsed extrema for centerline and damping

`compute_centerline_modal_signal()` should keep returning raw extrema diagnostics, but also expose collapsed extrema diagnostics. Centerline pairs and damping amplitudes should be built from the collapsed alternating extrema sequence, not raw candidates.

Rationale: the centerline should reflect physical swing midpoint samples. Raw candidates remain useful for visual debugging, but they should not drive modal estimates when they are noisy.

### D3: Notebook diagnostics compare raw versus collapsed extrema

Update the baseline compensation visualization to show:

- raw signal and centerline,
- raw extrema with low alpha,
- collapsed extrema with stronger markers,
- accepted peak/trough pair links,
- half peak-to-peak amplitude sequence used for damping.

Rationale: users need to see whether collapse tuning removed intra-lobe ripples without deleting real cycles.

## Risks / Trade-offs

- Reversal threshold too high -> real low-amplitude late decay cycles may be collapsed away, reducing damping samples. Mitigation: expose parameter and show collapsed extrema count.
- Reversal threshold too low -> noisy local extrema remain. Mitigation: default to existing centerline amplitude scale and keep visual diagnostics.
- Baseline drift can make lobe classification harder near zero. Mitigation: classification uses extrema kind and reversal amplitude, not zero-crossing.
- Duplicate or missing extrema can still leave too few valid pairs. Mitigation: keep existing fallback to mean centerline and zero damping.
