## Why

The AE spectral detector migrated from Arduino prototype publishes `FailureEvent::AcousticEmission` on nearly every FFT window, flooding the logger/MQTT pipeline at 30 events/minute. Root cause: the initial EWMA baseline sigma was set to 0.1 instead of the Arduino prototype's 10.0, making the first gradient detection threshold 0.6 instead of 60. This triggers danger on the ramp-up gradient and the baseline never recovers. The EWMA variance update formula also differs from the prototype, and the gradient is not clamped to >=0 as the Arduino does.

## What Changes

- Fix initial EWMA baseline variance from 0.01 to 100.0 (matching Arduino's `var_grad = 100.0`, sigma = 10.0).
- Fix EWMA variance update formula to match Arduino: `var = alpha * diff^2 + (1-alpha) * var` instead of `var = (1-alpha) * (var + alpha * diff^2)`.
- Add gradient clamping to >=0, matching Arduino's `if (gradien_kenaikan < 0) gradien_kenaikan = 0;`.
- Disable failure event publishing for energy-jump latch (`peringatan_retak`). Only the adaptive gradient danger (`status_patah`) publishes `FailureEvent::AcousticEmission`. Latch state tracking continues silently.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `ae-spectral-detector`: Correct baseline initialization, EWMA variance formula, and gradient clamping to match Arduino prototype behavior for the adaptive gradient danger detector.

## Impact

- Affected code:
  - `components/monitor/monitor.cpp` — `AeSpectralDetector::Reset()` (initial variance) and `AeSpectralDetector::UpdateEnergy()` (EWMA formula, gradient clamp, latch publish suppression).
- No Kconfig changes, no API changes, no hardware changes.
- Existing unit tests for spectral detector math may need updated expected values.
