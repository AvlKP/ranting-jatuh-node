## 1. Confirm Sources

- [x] 1.1 Re-read temp `components/monitor/monitor.cpp` `AeSpectralDetector::UpdateEnergy()` and current `components/monitor/ae_detector.cpp` before editing.
- [x] 1.2 Confirm current worktree changes in `components/monitor/*`, `sdkconfig`, and `sdkconfig.old` are user changes and avoid reverting unrelated edits.

## 2. Port Detector Algorithm

- [x] 2.1 Update `components/monitor/ae_detector.cpp` `AeSpectralDetector::UpdateEnergy()` so gradient ring write, oldest-index calculation, EWMA adaptation, threshold calculation, danger evaluation, write-index advance, and publish gate match temp source.
- [x] 2.2 Preserve existing `Reset()` variance/sigma behavior and update header initializers only if `UpdateEnergy()` can be called before `Reset()`.
- [x] 2.3 Keep ADC initialization, FFT energy calculation, AE task loop, dropped failure counter, and `PublishFailure(FailureEvent::AcousticEmission)` path unchanged except where needed for the temp publish gate.

## 3. Port Configuration

- [x] 3.1 Update project configuration so spectral ADC mode is selected for the target build.
- [x] 3.2 Set spectral tuning to temp values: 40000 Hz, 256 samples, bins 64..127, leak alpha 0.95, EWMA alpha 0.05, danger multiplier 15.0, gradient window 20, jump threshold 20.0, latch 2000 ms, publish interval 2000 ms.
- [x] 3.3 Keep component `Kconfig` defaults unchanged unless needed by project workflow; prefer target config for temp tuning.
- [x] 3.4 Confirm `main/main.cpp` still passes `ADC_CHANNEL_0` and `main/pins.hpp` still documents GPIO1 / ADC1_CH0.

## 4. Update Tests

- [x] 4.1 Update AE spectral unit tests that currently expect danger-only publication so they require active latch plus active danger.
- [x] 4.2 Add or update a test proving danger without latch does not publish.
- [x] 4.3 Add or update a test proving latch plus danger publishes when the interval allows it.
- [x] 4.4 Add or update a test proving `spectral_min_publish_interval_ms == 0` allows publication on every active window.
- [x] 4.5 Keep existing FFT energy, invalid config, initial sigma, EWMA, and negative-gradient clamp coverage passing.

## 5. Verify Specs

- [x] 5.1 Run OpenSpec validation/status for `port-ae-spectral-detector-eedays`.
- [x] 5.2 Confirm delta spec updates stale GPIO14 / ADC1_CH3 text to GPIO1 / ADC1_CH0.
- [x] 5.3 Confirm delta spec says latch activation alone does not publish, danger alone does not publish, and latch plus danger publishes.

## 6. Build And Test

- [x] 6.1 Run `idf.py build` in ESP-IDF v5.5.4 environment.
- [x] 6.2 Run `idf.py size` and inspect memory impact.
- [x] 6.3 Run `idf.py -B build-test -D TEST_COMPONENTS=monitor build`.
- [ ] 6.4 If hardware is available, flash/monitor spectral build and confirm `ae_spectral_task` starts and AE detections create `acoustic_emission` failure records.
