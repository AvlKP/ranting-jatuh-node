## Why

The working AE spectral detector in `temp/ranting-jatuh-node-eedays` contains hardware-tuned behavior that is newer than commit `4250607a9b623b71ee1aa245fd2895b80e26bdbe`, but current firmware has since refactored `monitor` into split files. Porting only the detector/config deltas keeps the team's tested algorithm intact without undoing current monitor structure, atomics, runtime safety work, or free-fall debounce.

## What Changes

- Port the temp `AeSpectralDetector::UpdateEnergy()` behavior into current `components/monitor/ae_detector.cpp`.
- Preserve current split-file monitor architecture; do not copy the monolithic temp `monitor.cpp`.
- Match the temp algorithm one-to-one for gradient ring update order, EWMA update order, danger threshold timing, latch state, and publish gate.
- Update AE spectral specs so latch-plus-danger publication semantics and the board ADC mapping match the implementation.
- Capture temp spectral runtime tuning: spectral ADC mode enabled, danger multiplier 15.0, jump threshold 20.0, 40 kHz sample rate, 256-sample window, bins 64..127, leak alpha 0.95, EWMA alpha 0.05, gradient window 20, latch 2000 ms, publish interval 2000 ms.
- Keep existing GPIO and simple ADC AE modes available through Kconfig.
- Update unit tests so they assert the temp detector behavior instead of the older danger-only behavior.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `ae-spectral-detector`: Align spectral detector algorithm, publish gate, ADC mapping, and tuned runtime configuration with the working temp implementation.

## Impact

- Affected code:
  - `components/monitor/ae_detector.cpp`
  - `components/monitor/test/test_monitor_algorithms.cpp`
  - `sdkconfig` and/or `sdkconfig.defaults`
  - `openspec/specs/ae-spectral-detector/spec.md`
- No public C++ API changes.
- No new dependencies.
- No logger, MQTT payload, SD CSV, or dashboard schema change.
- Hardware assumption retained from `main/pins.hpp`: AE analog input is GPIO1 / ADC1_CH0, not stale GPIO14 / ADC1_CH3 spec text.
