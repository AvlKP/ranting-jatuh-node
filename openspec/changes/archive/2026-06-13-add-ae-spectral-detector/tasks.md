## 1. Configuration and Wiring

- [x] 1.1 Add `CONFIG_MONITOR_AE_MODE_SPECTRAL_ADC` to `components/monitor/Kconfig` AE mode choice, keeping GPIO and simple ADC modes available
- [x] 1.2 Add spectral detector Kconfig defaults for sample rate 40000 Hz, window size 256, bin start 64, bin end 127, leak alpha 0.95, EWMA alpha 0.05, danger multiplier 6.0, gradient window 20, jump threshold 6.4, latch duration 2000 ms, and minimum publish interval
- [x] 1.3 Extend `MonitorConfig` in `components/monitor/include/monitor.hpp` with spectral detector parameters mapped from scaled Kconfig values
- [x] 1.4 Ensure `main/main.cpp` continues to pass board AE ADC channel GPIO14 / ADC1_CH3 and does not use Arduino GPIO34 or GPIO21

## 2. Spectral Detector Core

- [x] 2.1 Add a bounded detector type or monitor-owned helper for spectral state: previous energy, latch timestamp, integrator value, gradient ring, EWMA mean/variance/sigma, and active/publish state
- [x] 2.2 Implement Hamming-windowed 256-sample FFT processing using existing `esp-dsp` APIs and persistent interleaved float storage
- [x] 2.3 Implement high-frequency energy calculation over configured bins, defaulting to bins 64 through 127 and dividing summed magnitude by 1000.0
- [x] 2.4 Implement energy-jump latch detection with configured threshold and latch duration
- [x] 2.5 Implement leaking integrator, gradient window, EWMA baseline adaptation, sigma clamp, and dynamic danger threshold
- [x] 2.6 Add bounds checks or init failure for invalid spectral window and FFT bin configurations

## 3. ADC Continuous Acquisition

- [x] 3.1 Add spectral ADC initialization using `adc_continuous` for ADC1 single-channel sampling at configured sample rate
- [x] 3.2 Add a dedicated AE spectral processing task or equivalent monitor-owned task context separate from `Monitor::Update()`
- [x] 3.3 Collect ADC samples into fixed-size windows without heap allocation in the recurring hot path
- [x] 3.4 Stop compiling/running `adc_oneshot` AE threshold code when spectral ADC mode is selected
- [x] 3.5 Add task creation failure logging with task name, requested stack, free internal heap, and largest free internal block

## 4. Failure Publication

- [x] 4.1 Publish `FailureEvent::AcousticEmission` when energy-jump latch transitions inactive-to-active
- [x] 4.2 Publish `FailureEvent::AcousticEmission` when adaptive gradient danger transitions inactive-to-active
- [x] 4.3 Suppress unbounded repeated failure events during sustained active conditions using active-state tracking or configured minimum publish interval
- [x] 4.4 Reuse existing monitor dropped failure event counter for failed event posts from the spectral path
- [x] 4.5 Verify no status GPIO output or warning event type is introduced for spectral AE results

## 5. Tests

- [x] 5.1 Add unit tests for high-frequency energy bin summation with a synthetic FFT/window input
- [x] 5.2 Add unit tests for energy-jump latch activation and expiry behavior
- [x] 5.3 Add unit tests for leaking integrator, gradient buffer full gating, EWMA update, sigma clamp, and danger threshold detection
- [x] 5.4 Add unit tests for repeated active-condition suppression and failure publication edge triggering
- [x] 5.5 Add configuration tests or static assertions for invalid bin/window bounds where practical

## 6. Verification

- [x] 6.1 Build the default configuration to verify existing GPIO/simple AE behavior remains available
- [x] 6.2 Build with spectral ADC mode enabled using ESP-IDF v5.5.4 environment
- [x] 6.3 Record `idf.py size` output for spectral mode
- [ ] 6.4 Run monitor unit tests for spectral detector math and existing monitor algorithms
- [ ] 6.5 On hardware, verify boot logs show monitor, logger, and AE spectral task started
- [ ] 6.6 On hardware, verify acoustic emission detections create logger failure CSV and MQTT outbox records with event `acoustic_emission`
- [ ] 6.7 Record stack high-water margin for monitor, logger, and AE spectral task during spectral detection when task handle access is available
