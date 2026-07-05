## Why

The dashboard FFT plot stays flat or stale because `/api/status` reads `psd_accum_`, but the current modal FFT path never writes PSD bins into that buffer. This hides spectral output during debugging even when natural-frequency extraction runs.

## What Changes

- Restore dashboard-visible PSD publication from the monitor modal FFT path.
- Keep the existing `/api/status` JSON shape and dashboard JavaScript contract unchanged.
- Store PSD bins in `psd_accum_` whenever dominant-axis natural frequency FFT succeeds.
- Clear or leave zero PSD only when there is no valid FFT input or FFT execution fails.
- Add focused tests proving synthetic modal FFT data produces nonzero dashboard FFT data.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `free-decay-analysis`: The dominant-axis FFT requirement must also update the dashboard PSD buffer used by `GetFftData()`.
- `dashboard-fsm-adaptation`: The dashboard FFT plot must reflect the latest computed monitor PSD instead of a permanently zero/stale buffer.

## Impact

- Affected code: `components/monitor/modal_analyzer.cpp`, `components/monitor/sample_store.cpp`, `components/monitor/include/monitor.hpp`, and monitor Unity tests.
- Affected API: no public HTTP or MQTT schema change; `/api/status` keeps returning `"fft":[...]`.
- Runtime impact: bounded extra writes to an existing fixed-size `float[512]` buffer during post-hoc modal analysis only; no heap allocation and no new network or SD behavior.
