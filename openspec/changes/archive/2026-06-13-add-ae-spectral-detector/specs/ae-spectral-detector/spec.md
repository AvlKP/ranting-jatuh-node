## ADDED Requirements

### Requirement: Spectral acoustic emission detector mode
The monitor SHALL provide a Kconfig-selectable spectral ADC acoustic emission detector mode that samples the configured AE ADC channel, computes high-frequency spectral energy, and publishes acoustic emission failures through the existing monitor failure event pipeline.

#### Scenario: Spectral mode initializes ADC sampling
- **WHEN** firmware boots with spectral acoustic emission mode enabled
- **THEN** the monitor SHALL configure the board AE ADC channel for continuous sampling
- **AND** the sampling rate SHALL default to 40000 Hz
- **AND** the detector window length SHALL default to 256 samples

#### Scenario: Spectral mode uses board AE ADC pin
- **WHEN** application code creates the monitor configuration for the custom PCB
- **THEN** the spectral detector SHALL use the configured `ae_adc_channel`
- **AND** the board mapping SHALL remain GPIO14 / ADC1_CH3
- **AND** Arduino prototype pins GPIO34 and GPIO21 SHALL NOT be used

### Requirement: Detector SHALL compute high-frequency energy from FFT bins
The spectral detector SHALL transform each full sample window with a Hamming window and FFT, then compute high-frequency energy from the configured FFT bin range.

#### Scenario: Default bin energy calculation
- **WHEN** the detector has collected 256 ADC samples
- **THEN** it SHALL apply a Hamming window to the sample window
- **AND** it SHALL compute FFT magnitudes using ESP-IDF-compatible DSP code
- **AND** it SHALL sum magnitude bins 64 through 127 by default
- **AND** it SHALL divide the summed energy by 1000.0 before state detection

#### Scenario: Configured bin range is bounded
- **WHEN** configured spectral bin start or end values fall outside the valid positive-frequency FFT range
- **THEN** initialization SHALL fail or clamp the range before sampling starts
- **AND** the detector SHALL NOT read outside its FFT output buffer

### Requirement: Energy jump latch SHALL publish acoustic emission failure
The detector SHALL treat the prototype crack latch condition as an acoustic emission failure event.

#### Scenario: Energy jump exceeds threshold
- **WHEN** current high-frequency energy minus previous high-frequency energy exceeds the configured jump threshold
- **THEN** the detector SHALL mark the latch active for the configured latch duration
- **AND** it SHALL publish `FailureEvent::AcousticEmission` through the existing monitor failure event pipeline

#### Scenario: Latch expires
- **WHEN** the detector latch is active
- **AND** the configured latch duration has elapsed without a new energy jump
- **THEN** the detector SHALL clear the latch state
- **AND** it SHALL NOT require any status output GPIO change

### Requirement: Adaptive gradient danger SHALL publish acoustic emission failure
The detector SHALL implement the prototype leaking integrator, gradient buffer, EWMA statistics, and dynamic danger threshold, and SHALL treat danger detection as an acoustic emission failure.

#### Scenario: Gradient exceeds dynamic danger threshold
- **WHEN** the leaking integrator gradient is greater than `mu_grad + (K_BAHAYA * sigma_grad)`
- **AND** the gradient buffer is full
- **THEN** the detector SHALL publish `FailureEvent::AcousticEmission` through the existing monitor failure event pipeline

#### Scenario: Normal gradient adapts baseline
- **WHEN** the gradient z-score is less than or equal to 3.0
- **THEN** the detector SHALL update EWMA mean and variance using configured alpha values
- **AND** sigma SHALL be clamped to at least 0.1

### Requirement: Spectral detector SHALL avoid failure event floods
The spectral detector SHALL avoid unbounded repeated acoustic emission failure publication while a detection condition remains continuously active.

#### Scenario: Sustained active condition
- **WHEN** the energy latch or adaptive gradient danger condition remains active across consecutive FFT windows
- **THEN** the detector SHALL publish failure events only on inactive-to-active transitions or according to a bounded configured minimum publish interval
- **AND** failed event posts SHALL increment the existing monitor dropped failure event counter

### Requirement: Spectral detector SHALL not introduce a warning output
The spectral detector SHALL route all prototype warning and danger outputs as acoustic emission failure events and SHALL NOT drive a dedicated status GPIO.

#### Scenario: Prototype warning condition occurs
- **WHEN** the condition corresponding to `peringatan_retak` occurs
- **THEN** the firmware SHALL publish `FailureEvent::AcousticEmission`
- **AND** the firmware SHALL NOT publish a warning event type for acoustic emission
- **AND** the firmware SHALL NOT drive the Arduino prototype status pin

#### Scenario: Prototype danger condition occurs
- **WHEN** the condition corresponding to `status_patah` occurs
- **THEN** the firmware SHALL publish `FailureEvent::AcousticEmission`
- **AND** the existing logger failure pipeline SHALL handle SD storage and MQTT outbox publication
