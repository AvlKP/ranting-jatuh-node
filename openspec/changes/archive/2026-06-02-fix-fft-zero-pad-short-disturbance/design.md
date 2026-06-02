## Context

`ComputeNaturalFrequency()` currently requires exactly `kFftWindowSamples` (1024) samples to compute the FFT. When called with fewer samples during a short DISTURBED→IDLE transition, it zeroes `psd_accum_` and returns false. The dashboard reads zeroed data and shows -120 dB flat. The ESP-DSP FFT engine is initialized for N=1024 via `dsps_fft2r_init_fc32(nullptr, 1024)` and supports any power-of-2 N ≤ 1024.

## Goals / Non-Goals

**Goals:**
- Always produce a valid PSD estimate from any non-empty DISTURBED buffer upon transition.
- Maintain dashboard compatibility — no changes to `GetFftData()` output size or dashboard JS.
- Preserve existing Welch averaging for ≥1024 samples.

**Non-Goals:**
- Improving FFT resolution for short disturbances (zero-padding interpolates, doesn't add information).
- Changing the state machine transition logic.
- Dashboard UI changes to indicate zero-padded vs full FFT.

## Decisions

### Decision: Three-tier FFT sizing
- *Choice:* `count < 512` → 512-pt FFT, `512 ≤ count < 1024` → 1024-pt FFT, `count ≥ 1024` → Welch (unchanged).
- *Alternative:* Always use 1024-pt with zero-padding. Rejected because very short disturbances (e.g. 50 samples) zero-padded to 1024 would be ~95% zeros — the 512-pt FFT gives a more honest spectral estimate.
- *Alternative:* Keep last valid `psd_accum_` (don't zero). Rejected because stale data from a previous disturbance is misleading.

### Decision: 512-pt bin-to-slot mapping
- *Choice:* Duplicate each of 256 output bins into two consecutive `psd_accum_` slots (`psd_accum_[2i] = psd_accum_[2i+1] = bin[i]`).
- *Rationale:* `psd_accum_` is 512 slots. Dashboard downsamples 512→128 and computes frequency labels assuming 1024-pt FFT. Duplicating preserves correct frequency axis alignment. Each pair averages to the same value during downsample.

### Decision: Single-window (no Welch) for padded cases
- *Choice:* Padded cases use a single Hann-windowed FFT segment.
- *Rationale:* Zero-padded regions contribute no signal, so overlapping segments would just average with noise floor. Single window is correct.

### Decision: Reuse existing twiddle table
- *Choice:* No additional `dsps_fft2r_init_fc32()` call. ESP-DSP internally supports `dsps_fft2r_fc32(data, 512)` when initialized for N=1024.
- *Rationale:* Twiddle factors for N/2 are a subset of those for N.

## Risks / Trade-offs

- **Broad peaks for short disturbances** → Expected. Zero-padding interpolates the spectrum but can't invent resolution. Users see *something* rather than -120 dB. Acceptable.
- **ESP-DSP 512-pt assumption** → If `dsps_fft2r_fc32(data, 512)` fails with a 1024-init'd table, fallback: init for both sizes in `Monitor::Init()`. Low risk given radix-2 FFT properties.
