## Change

One line in `monitor.cpp`, in `ComputeAndPublish()`, before the FFT computation section:

```cpp
if (pub_state == NodeState::DISTURBED) {
    static_cast<void>(ComputeSwayAndDamping(result));
    if (is_exit) {
        psd_accum_.fill(0.0f);  // ← ADD: reset before new analysis

        PeakList roll_peaks, pitch_peaks;
        DecayRegion roll_decay = FindDecayRegion(roll_history_, roll_peaks);
        // ... rest unchanged
```

## Why Here

`ComputeAndPublish(is_exit=true)` is called exactly once per DISTURBED→IDLE transition. This is when FFT and damping are computed. Resetting `psd_accum_` here ensures:

- Each analysis event starts with a clean accumulator
- Welch averaging within the event still works (multiple segments accumulate into `psd_accum_` during the `ComputeAxisNaturalFrequency` loop)
- The dashboard always shows the most recent event's spectrum

## What Stays the Same

- `psd_accum_` still accumulates power across FFT segments within a single analysis event (Welch method)
- `GetFftData()` still reads `psd_accum_` — no API change
- Dashboard JavaScript unchanged — it reads the same JSON field
- `local_psd` computation unchanged — natural frequency result unaffected
- Debug CSV logging of `psd_accum_` (from `improve-debug-csv-logging`) captures the correct per-event cumulative spectrum
