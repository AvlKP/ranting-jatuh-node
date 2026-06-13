## Context

The monitor component already owns acoustic emission detection and publishes `FailureEvent::AcousticEmission` through the ESP event loop. Logger and network code already persist and publish failure events, so the new detector should produce the same failure event instead of introducing a warning channel or a status GPIO.

The existing ADC mode reads one ADC sample from `Monitor::Update()` at the IMU rate. That cannot represent the Arduino prototype because the prototype needs 40 kHz sampling, a 256-sample FFT, and stateful spectral energy analysis. The firmware already depends on `esp-dsp`, so the FFT portion can use the existing ESP-IDF-compatible DSP dependency instead of ArduinoFFT.

## Goals / Non-Goals

**Goals:**
- Port the Arduino spectral detector behavior into ESP-IDF using ESP32-S3 ADC and `esp-dsp`.
- Sample the AE analog input at the configured spectral detector rate, default 40 kHz.
- Publish `FailureEvent::AcousticEmission` when either the energy-jump latch or adaptive gradient danger condition triggers.
- Reuse the existing logger failure pipeline for SD storage and MQTT outbox publication.
- Keep all recurring buffers bounded and persistent, with no heap allocation in hot paths.
- Keep GPIO interrupt and simple ADC threshold modes available through Kconfig unless explicitly disabled by configuration.

**Non-Goals:**
- No Arduino compatibility layer or ArduinoFFT dependency.
- No status/danger output GPIO. Logger failure handling is the output path.
- No claim that the detector is true acoustic emission physics.
- No logger schema expansion unless failure payload detail is separately requested.
- No dashboard plotter clone for the five Arduino serial columns in this change.

## Decisions

### Add a spectral ADC mode instead of replacing existing AE modes

Add a new Kconfig choice entry such as `CONFIG_MONITOR_AE_MODE_SPECTRAL_ADC`. This preserves the existing digital interrupt and simple ADC threshold modes for bring-up and fallback.

Alternative considered: replace `MONITOR_AE_MODE_ADC`. Rejected because the existing threshold mode is simpler and useful for hardware validation.

### Run spectral acquisition in a dedicated AE processing task

Use `adc_continuous` with DMA-backed reads in a dedicated task, separate from the 52 Hz IMU monitor loop. The task collects 256 samples, runs the spectral detector, and publishes failure events through the existing monitor event path or a monitor-owned helper.

Alternative considered: sample ADC inside `Monitor::Update()`. Rejected because 40 kHz sampling and 256-sample FFT windows do not fit the IMU-rate task cadence.

Alternative considered: busy-wait with `esp_timer_get_time()` like Arduino `micros()`. Rejected because it wastes CPU and power, and can disturb IMU, logger, WiFi, and dashboard scheduling.

### Use `esp-dsp` complex FFT with persistent working storage

Convert raw ADC samples into a persistent interleaved float FFT buffer, apply a Hamming window, run the existing `dsps_fft2r_fc32` path, and sum magnitudes for bins 64 through 127. Keep buffers as class members or static storage owned by the AE detector, not stack locals.

Alternative considered: add another FFT library. Rejected because `esp-dsp` is already integrated and supports ESP32-S3 acceleration paths.

### Preserve Arduino detector constants as configurable scaled Kconfig values

Defaults should match the prototype:
- samples: 256
- sample rate: 40000 Hz
- bin start: 64
- bin end: 127
- leak alpha: 0.95
- EWMA alpha: 0.05
- danger multiplier: 6.0
- gradient window: 20
- jump threshold: 6.4
- latch duration: 2000 ms

Use scaled integer Kconfig values where ESP-IDF Kconfig cannot represent floats directly.

Alternative considered: hard-code constants. Rejected because this detector is empirical and will need tuning on the custom PCB.

### Convert both prototype outputs into failure events

The Arduino prototype has `status_patah` and `peringatan_retak`. In this firmware, both are treated as acoustic emission failures. The status GPIO is discarded. To avoid flooding logger/network with repeated events, publish on inactive-to-active transitions and optionally enforce a configurable minimum publish interval.

Alternative considered: add a warning event type. Rejected by user direction: AE event is a failure event, not a warning.

## Risks / Trade-offs

- High CPU load from 40 kHz ADC plus FFT every 6.4 ms -> measure task stack, CPU headroom, event drops, and heap after enabling spectral mode.
- Event flood during sustained signal -> publish only on detector state transitions and keep dropped-event counters visible.
- ADC continuous driver conflicts with simple ADC one-shot mode -> compile only one AE ADC mode at a time and keep ownership clear.
- WiFi can affect ADC noise and timing -> validate thresholds on target PCB with WiFi/logger active.
- Arduino constants may not transfer to ESP32-S3 ADC scaling -> expose thresholds via Kconfig and verify with recorded signal cases.
- Added task consumes RAM and stack -> keep buffers persistent, add compile-time size checks, and record `idf.py size`.

## Migration Plan

1. Add `CONFIG_MONITOR_AE_MODE_SPECTRAL_ADC` and spectral detector tuning keys.
2. Implement the detector behind that mode without changing existing GPIO and simple ADC modes.
3. Route detector output to existing `FailureEvent::AcousticEmission`.
4. Add unit tests for detector math and state transitions using synthetic windows.
5. Build default and spectral configurations.
6. Validate on hardware with AE ADC connected to GPIO14 / ADC1_CH3.

Rollback: switch Kconfig back to `MONITOR_AE_MODE_GPIO` or `MONITOR_AE_MODE_ADC`.

## Open Questions

- Should spectral mode be the default after validation, or stay opt-in until thresholds are tuned on hardware?
- What minimum interval should suppress repeated `AcousticEmission` failure publishes during sustained high energy?
- Do we need a diagnostic-only dashboard/status endpoint for latest spectral energy, gradient, threshold, and latch state?
