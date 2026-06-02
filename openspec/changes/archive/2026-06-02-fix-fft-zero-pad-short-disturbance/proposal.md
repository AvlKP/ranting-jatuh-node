## Why

When a short disturbance ends (DISTURBED → IDLE transition with fewer than 1024 accumulated samples), `ComputeNaturalFrequency()` zeroes out `psd_accum_` and returns false. The dashboard then reads all-zero PSD data, displaying -120 dB flat across all frequencies. The FFT graph should always show the best-available spectral estimate for any disturbance, regardless of duration.

## What Changes

- Modify `ComputeNaturalFrequency()` to support variable FFT sizes with zero-padding instead of bailing when sample count is below 1024.
- For disturbances with < 512 samples: zero-pad to 512 and compute a single 512-point FFT.
- For disturbances with 512–1023 samples: zero-pad to 1024 and compute a single 1024-point FFT.
- For ≥ 1024 samples: existing Welch averaging behavior unchanged.
- Map 512-point FFT bins (256 output bins) into the 512-slot `psd_accum_` array by duplicating each bin into two consecutive slots, preserving dashboard frequency axis alignment.

## Capabilities

### New Capabilities
- `fft-zero-pad`: Adaptive FFT sizing with zero-padding for short sample buffers, replacing the current all-or-nothing 1024-point requirement.

### Modified Capabilities

_(none — no existing spec-level requirements change; the node-state-machine spec does not mandate FFT behavior)_

## Impact

- **Code**: `components/monitor/monitor.cpp` — `ComputeNaturalFrequency()` only.
- **APIs**: No changes. `GetFftData()` still returns 512 floats. Dashboard JS unchanged.
- **Dependencies**: ESP-DSP `dsps_fft2r_fc32` already supports N ≤ init size (init'd at 1024, calling with 512 is valid).
- **Risk**: Minimal. Zero-padded FFTs produce broader spectral peaks and lower SNR, but this is inherent to short observation windows and preferable to showing nothing.
