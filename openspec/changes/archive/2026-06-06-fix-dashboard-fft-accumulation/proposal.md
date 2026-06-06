## Why

The dashboard FFT chart shows `psd_accum_` — a cumulative sum of PSD power across ALL analysis events since boot. This array is never reset:

```
Event 1 (8 Hz branch poke)  →  psd_accum_ = PSD₁
Event 2 (12 Hz branch poke) →  psd_accum_ = PSD₁ + PSD₂
Event 3 (10 Hz branch poke) →  psd_accum_ = PSD₁ + PSD₂ + PSD₃
```

After 3 events the dashboard shows peaks at 8, 10, and 12 Hz simultaneously, with a noise floor that grows without bound. This is misleading — the user expects to see the frequency content of the most recent disturbance, not a historical sum.

The computed `natural_freq_hz` (used for failure detection) is correct — it comes from `local_psd`, the per-event spectrum. Only the dashboard display is wrong.

The `improve-debug-csv-logging` change adds cumulative PSD columns to FFT.CSV for comparison. This proposal fixes the dashboard side so both display and CSV reflect per-event data.

## What Changes

- Zero `psd_accum_` at the start of each DISTURBED→IDLE analysis event in `ComputeAndPublish()`
- Dashboard FFT chart shows the most recent event's PSD instead of a cumulative sum
- No API changes, no new members, no dashboard code changes

## Capabilities

### Modified Capabilities
- `dashboard-fft-display`: Dashboard FFT chart SHALL display the per-event power spectral density of the most recent analysis event, not an unbounded cumulative sum across all events.

## Impact

- Affected code: `monitor.cpp` — one line addition (`psd_accum_.fill(0.0f)`) in `ComputeAndPublish()`
- No dashboard changes, no API changes, no dependency changes
- `psd_accum_` still accumulates during the Welch FFT loop (multiple segments within one event) — only the cross-event accumulation is removed
