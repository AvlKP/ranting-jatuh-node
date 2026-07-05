## Context

The dashboard status endpoint already emits an `"fft"` array by calling `Monitor::GetFftData()`. The browser redraws the FFT chart whenever that array is non-empty. `GetFftData()` copies `psd_accum_`, but current source only declares and reads that buffer; no modal analysis path writes new PSD values into it.

The dominant-axis FFT path in `ComputeSignedAxisNaturalFrequency()` computes bin power to select `natural_freq_hz`, then discards those powers. This makes the dashboard plot flat at the log safety floor even though scalar natural-frequency extraction can still work.

## Goals / Non-Goals

**Goals:**

- Make the dashboard FFT plot reflect the latest successful dominant-axis modal FFT.
- Keep the existing `/api/status` field name, JSON shape, downsampling, and JavaScript update path unchanged.
- Keep memory bounded by reusing the existing `psd_accum_` buffer.
- Preserve real-time constraints: no heap allocation, no blocking I/O, no work in ISR paths.
- Add focused test coverage for dashboard-visible PSD data.

**Non-Goals:**

- Redesign the dashboard chart UI.
- Add new HTTP fields, MQTT fields, or SD log columns.
- Add continuous FFT computation during IDLE sampling.
- Change acoustic-emission spectral detector output.
- Change modal peak-selection thresholds or damping math except where needed to store PSD.

## Decisions

### 1. Store PSD where modal FFT already computes bin power

`ComputeSignedAxisNaturalFrequency()` is the right producer because it already owns the windowed dominant-axis FFT and iterates over complex FFT bins. After successful FFT execution, it should write power values into `psd_accum_` before returning the selected frequency.

Alternative: compute dashboard PSD separately in `GetFftData()`. Rejected because it would do expensive FFT work inside the HTTP request path and hold monitor synchronization near dashboard I/O.

Alternative: compute PSD on every sample. Rejected because the dashboard only needs post-hoc event spectra and continuous FFT would add recurring monitor-task cost.

### 2. Keep `psd_accum_` as the dashboard contract

The existing fixed-size `float[512]` PSD buffer remains the monitor-dashboard boundary. The dashboard already downsamples 512 bins to 128 response values and computes labels assuming `fftSize = data.fft.length * 2` after downsampling.

For 1024-point FFTs, write 512 positive-frequency bins directly. For 512-point FFTs, duplicate each of 256 bins into two adjacent 512-slot entries so the existing dashboard downsampling path still receives 512 slots.

Alternative: make `/api/status` report the actual FFT size. Rejected for this fix because it changes the HTTP contract and dashboard JavaScript for a producer-side regression.

### 3. Clear PSD on failed or invalid FFT inputs

When the dominant-axis FFT cannot run because the segment is too short, the bin range is invalid, or ESP-DSP fails, `psd_accum_` should be zeroed so the dashboard does not show stale spectrum from a previous event.

Alternative: keep last valid PSD until next success. Rejected because stale data would imply the current event produced a spectrum.

### 4. Keep locking simple and bounded

Modal analysis runs outside `PushSample()`'s mutex today. Implementation should either update `psd_accum_` under the monitor mutex after FFT powers are computed, or use a small helper with a bounded copy. It must not hold the mutex while running ESP-DSP FFT.

## Risks / Trade-offs

- **Short events produce broad spectra** -> Mitigation: preserve existing adaptive FFT sizing behavior and only restore visibility of available PSD.
- **PSD update races dashboard snapshot** -> Mitigation: copy/update the fixed 512-float buffer under existing monitor synchronization.
- **Incorrect bin mapping skews dashboard frequency labels** -> Mitigation: test both 512-point duplication and 1024-point direct mapping, or at minimum test the synthetic peak reaches `GetFftData()`.
- **Flat plot remains before first valid event** -> Mitigation: expected behavior; no valid modal FFT means zero PSD.

## Migration Plan

1. Add monitor-side PSD update helper or bounded write path.
2. Wire successful dominant-axis FFT powers into `psd_accum_`.
3. Zero `psd_accum_` on invalid or failed FFT paths.
4. Add Unity coverage for nonzero dashboard-visible PSD after synthetic modal FFT.
5. Build the monitor test configuration and validate on hardware by triggering a disturbance and checking `/api/status` FFT values are not all zero.

Rollback is simple: revert this change. No persistent data or external schema migration is involved.
