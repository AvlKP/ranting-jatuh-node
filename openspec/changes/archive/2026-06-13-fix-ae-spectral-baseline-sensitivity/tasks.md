## 1. Fix Detector Baseline Initialization

- [x] 1.1 Change `ewma_variance_{0.01f}` to `ewma_variance_{100.0f}` in `AeSpectralDetector::Reset()` at `monitor.cpp:216`
- [x] 1.2 Change `sigma_{0.1f}` to `sigma_{10.0f}` in `AeSpectralDetector::Reset()` at `monitor.cpp:217`

## 2. Fix EWMA Variance Update Formula

- [x] 2.1 Replace the variance update in `AeSpectralDetector::UpdateEnergy()` at `monitor.cpp:265-266` with the Arduino formula: `ewma_variance_ = (config.spectral_ewma_alpha * diff * diff) + ((1.0f - config.spectral_ewma_alpha) * ewma_variance_);`

## 3. Add Gradient Zero Clamp

- [x] 3.1 Add `if (gradient < 0.0f) gradient = 0.0f;` in `AeSpectralDetector::UpdateEnergy()` after line 254 (after gradient is computed from ring buffer)

## 4. Suppress Latch Failure Event Publishing

- [x] 4.1 Remove `result.latch_started` from `should_publish` in `AeSpectralDetector::UpdateEnergy()` at `monitor.cpp:285`
- [x] 4.2 Change `was_active` to `was_danger_active` (remove latch from combined active state) at `monitor.cpp:232`
- [x] 4.3 Change `interval_due` computation to use `danger_active_` instead of combined `active` (latch + danger) at `monitor.cpp:271-275`
- [x] 4.4 Verify `latch_active`, `latch_started`, `latch_until_ms_` state tracking still functional in result struct

## 5. Update Unit Tests

- [x] 5.1 Update test expected values in `components/monitor/test/*` for initial EWMA mean/variance/sigma after `Reset()` to reflect sigma=10.0
- [x] 5.2 Add test case verifying gradient is clamped to >=0 when integrator decreases
- [x] 5.3 Update energy-jump latch tests to assert latch state tracks correctly but does NOT trigger `should_publish`
- [x] 5.4 Verify adaptive gradient danger tests still trigger `should_publish` correctly

## 6. Build and Verify

- [x] 6.1 Build with spectral ADC mode enabled using ESP-IDF v5.5.4 environment
- [x] 6.2 Run monitor unit tests and confirm all pass
- [x] 6.3 Record `idf.py size` output to confirm no RAM increase
