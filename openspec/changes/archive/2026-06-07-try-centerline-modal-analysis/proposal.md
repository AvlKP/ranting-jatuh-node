## Why

Pull-and-release tests show a pitch jump followed by slow baseline decay while oscillation continues around that moving baseline. Current notebook metrics de-mean the whole segment and use absolute extrema from zero, so slow baseline motion can corrupt natural frequency and damping estimates.

## What Changes

- Add notebook-only centerline modal analysis for disturbed tilt segments.
- Change notebook disturbance HPF default cutoff to 0.2 Hz.
- Restrict notebook FFT peak search to 0.5 Hz through 12 Hz for current logs.
- Compute damping from peak/trough half peak-to-peak amplitudes after centerline removal.
- Add diagnostics for raw pitch, estimated centerline, residual pitch, peak pairs, envelope, and bounded PSD.
- Record that production can later raise IMU ODR to 52 Hz, but firmware changes are out of scope here.

## Capabilities

### New Capabilities
- `notebook-centerline-modal-analysis`: Notebook workflow for baseline-robust modal analysis of pull-and-release IMU logs.

### Modified Capabilities

## Impact

- Affects `notebook/disturbance_detection.py`, `notebook/natural_frequency.py`, `notebook/analysis.ipynb`, and `notebook/NOTES.md`.
- No production firmware, MQTT, logger, dashboard, or stored data format changes.
