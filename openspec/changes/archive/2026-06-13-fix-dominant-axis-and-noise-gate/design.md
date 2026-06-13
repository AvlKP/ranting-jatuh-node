## Context

The current `Monitor::ComputeDominantAxisSway()` in `monitor.cpp:1546` selects the dominant gyro axis by computing the cumulative sum of each gyro axis over the event segment:

```cpp
for (i = start; i < end; ++i) {
    sx_deg += gx_history_[i] * dt;
    sy_deg += gy_history_[i] * dt;
    sz_deg += gz_history_[i] * dt;
}
dominant = argmax(|sx|, |sy|, |sz|);
```

For symmetric oscillation (typical branch decay), the cumulative sum approaches zero on all axes, making axis selection unreliable.

The Python reference in `imu_algorithms/_extraction.py` computes peak-to-peak angular displacement:

```python
angle = np.cumsum(active) * dt
return float(angle.max() - angle.min())
```

This correctly measures oscillation amplitude regardless of symmetry.

Additionally, the Python reference gates damping computation behind `is_dynamic()` which rejects events with peak_gmag < 8.0 and duration < 80 samples. The C++ code has no such gate, computing damping on all disturbances including noise-level flick events which produce unreliable damping values.

## Goals / Non-Goals

**Goals:**
- Fix dominant axis selection to use peak-to-peak angular displacement, matching Python reference
- Add noise gate to skip damping on low-signal-energy events
- Make noise gate threshold configurable via Kconfig
- Keep O(1) per-sample complexity in real-time path (dominant axis is post-hoc only)

**Non-Goals:**
- Do NOT introduce semantic event type classification (pull_release, oscillation, flick, pull_hold)
- Do NOT add zero-crossing or peak-to-peak frequency estimation
- Do NOT add active region extraction
- Do NOT change MQTT payload schema
- Do NOT change Python reference code

## Decisions

### Decision 1: Peak-to-peak on cumulative angle for dominant axis

**Choice:** Track min and max of cumulative angle per axis in a single pass, return max - min.

**Rationale:** Matches Python reference. Single-pass O(N) with two accumulators per axis (cumulative sum + min/max tracking). No additional memory beyond existing 3 floats for min and 3 for max.

**Alternative considered:** Keep cumulative sum but take absolute value. Rejected because cumulative sum still approaches zero for symmetric oscillation regardless of sign.

### Decision 2: Simple peak-gmag threshold for noise gate

**Choice:** If `peak_gmag < CONFIG_MONITOR_NOISE_GATE_GMAG_X10 / 10.0`, skip damping. Default threshold 8.0 dps.

**Rationale:** Matches the spirit of Python's `is_dynamic()` peak_gmag >= 8.0 condition without the duration-based sub-classification (which would require tracking onset/offset times and a decision tree). Peak gmag is already tracked during DISTURBED in `peak_gmag` (Python equivalent) or can be computed by scanning gmag_history.

**Alternative considered:** RMS-based energy threshold. Rejected because RMS requires full scan and the peak_gmag is simpler to compute and already available conceptually (Python reference uses `event.peak_gmag`).

### Decision 3: Noise gate placed in AnalyzeImuEvent

**Choice:** Gate at start of `AnalyzeImuEvent()`, after decay onset detection succeeds but before dominant axis and FFT computation. If gated, skip FFT and damping entirely.

**Rationale:** Prevents wasted FFT/damping computation on noise events. If peak gmag is below threshold, there's no useful signal to analyze.

**Alternative considered:** Gate only damping, still compute frequency. Rejected because if the signal is pure noise, FFT frequency is also meaningless. However, the spec says natural frequency SHALL still be computed — so the gate will only skip damping, not FFT.

### Decision 4: Peak gmag source

**Choice:** Compute peak gmag from `gmag_history_` over the full buffer during `AnalyzeImuEvent()`, OR track it during DISTURBED in a member variable.

**Rationale:** Tracking during DISTURBED avoids a full buffer scan. Add `float peak_gmag_{0.0f}` member to Monitor class, updated in `PushSample()` when state is DISTURBED.

**Alternative considered:** Scan gmag_history_ in AnalyzeImuEvent. Rejected because it adds O(N) scan that's avoidable since we already iterate samples in PushSample.

## Risks / Trade-offs

- [Risk] Peak-to-peak dominant axis may select a different axis than before on symmetric oscillations, changing published natural frequency and damping values. → Mitigation: This is an improvement — the new axis is the correct one. Existing CSV logs won't change.
- [Risk] Noise gate may drop damping on borderline events that previously produced medium-confidence results. → Mitigation: Threshold is configurable and defaults to a conservative 8.0 dps. Users can lower it if needed.
- [Trade-off] Tracking peak_gmag in PushSample adds 1 float comparison per sample in DISTURBED state. Negligible overhead (O(1), single compare).

## Open Questions

- Should the noise gate threshold match the existing `dsp_gmag_onset_dps` (2.0 dps default) or be independent? Decision: independent, default to 8.0 dps matching Python reference.
