## MODIFIED Requirements

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
- **AND** the board mapping SHALL remain GPIO1 / ADC1_CH0
- **AND** Arduino prototype pins GPIO34 and GPIO21 SHALL NOT be used

#### Scenario: Target spectral configuration matches working temp build
- **WHEN** the project target configuration is set for the team's spectral AE detector
- **THEN** `MONITOR_AE_MODE_SPECTRAL_ADC` SHALL be selected
- **AND** `MONITOR_AE_SPECTRAL_SAMPLE_RATE_HZ` SHALL be 40000
- **AND** `MONITOR_AE_SPECTRAL_WINDOW_SIZE` SHALL be 256
- **AND** `MONITOR_AE_SPECTRAL_BIN_START` SHALL be 64
- **AND** `MONITOR_AE_SPECTRAL_BIN_END` SHALL be 127
- **AND** `MONITOR_AE_SPECTRAL_LEAK_ALPHA_X100` SHALL be 95
- **AND** `MONITOR_AE_SPECTRAL_EWMA_ALPHA_X100` SHALL be 5
- **AND** `MONITOR_AE_SPECTRAL_DANGER_MULTIPLIER_X10` SHALL be 150
- **AND** `MONITOR_AE_SPECTRAL_GRADIENT_WINDOW` SHALL be 20
- **AND** `MONITOR_AE_SPECTRAL_JUMP_THRESHOLD_X10` SHALL be 200
- **AND** `MONITOR_AE_SPECTRAL_LATCH_DURATION_MS` SHALL be 2000
- **AND** `MONITOR_AE_SPECTRAL_MIN_PUBLISH_INTERVAL_MS` SHALL be 2000

### Requirement: Energy jump latch SHALL track state without publishing failure events
The detector SHALL track the energy-jump latch state internally for diagnostics and as an acoustic-emission publish prerequisite, but SHALL NOT publish `FailureEvent::AcousticEmission` when the latch activates by itself.

#### Scenario: Energy jump exceeds threshold
- **WHEN** current high-frequency energy minus previous high-frequency energy exceeds the configured jump threshold
- **THEN** the detector SHALL mark the latch active for the configured latch duration
- **AND** the detector SHALL NOT publish a failure event unless adaptive gradient danger is also active and the publish interval allows it

#### Scenario: Latch expires
- **WHEN** the detector latch is active
- **AND** the configured latch duration has elapsed without a new energy jump
- **THEN** the detector SHALL clear the latch state
- **AND** it SHALL NOT require any status output GPIO change

#### Scenario: Latch gates acoustic emission publication
- **WHEN** adaptive gradient danger is active
- **AND** the energy-jump latch is not active
- **THEN** the detector SHALL NOT publish `FailureEvent::AcousticEmission`

### Requirement: Adaptive gradient danger SHALL publish acoustic emission failure
The detector SHALL implement the working temp leaking integrator, gradient buffer, EWMA statistics, and dynamic danger threshold, and SHALL publish acoustic emission failure only when adaptive gradient danger and energy-jump latch are both active.

#### Scenario: Gradient exceeds dynamic danger threshold while latch is active
- **WHEN** the leaking integrator gradient is greater than `mu_grad + (K_BAHAYA * sigma_grad)`
- **AND** the gradient has been clamped to a minimum of zero
- **AND** the gradient buffer is full
- **AND** the energy-jump latch is active
- **AND** the configured minimum publish interval allows publication
- **THEN** the detector SHALL publish `FailureEvent::AcousticEmission` through the existing monitor failure event pipeline

#### Scenario: Normal gradient adapts baseline before threshold comparison
- **WHEN** the gradient z-score is less than or equal to 3.0
- **THEN** the detector SHALL update EWMA mean using `mu = (alpha * grad) + ((1 - alpha) * mu)`
- **AND** the detector SHALL update EWMA variance using `var = (alpha * diff_new^2) + ((1 - alpha) * var)` after mean update
- **AND** sigma SHALL be clamped to at least 0.1
- **AND** the detector SHALL compute the danger threshold after EWMA adaptation
- **AND** the initial EWMA variance SHALL be 100.0 (sigma = 10.0) matching the Arduino prototype default

#### Scenario: Gradient ring update order matches temp implementation
- **WHEN** the detector processes one spectral energy value
- **THEN** it SHALL write the current leaking integrator value to the current gradient-ring index before computing gradient
- **AND** once the gradient window is full, it SHALL compute the oldest index as `(gradient_write_index + 1) % spectral_gradient_window`
- **AND** it SHALL advance the gradient write index after danger evaluation

#### Scenario: Negative gradient is zeroed before detection
- **WHEN** the computed leaking-integrator gradient is negative
- **THEN** the gradient SHALL be clamped to zero before EWMA baseline adaptation and before danger threshold comparison

### Requirement: Spectral detector SHALL avoid failure event floods
The spectral detector SHALL avoid unbounded repeated acoustic emission failure publication while the temp publish condition remains continuously active.

#### Scenario: Sustained active condition
- **WHEN** both the energy latch and adaptive gradient danger condition remain active across consecutive FFT windows
- **THEN** the detector SHALL publish failure events only when `last_publish_ms` is zero or the configured minimum publish interval has elapsed
- **AND** failed event posts SHALL increment the existing monitor dropped failure event counter

#### Scenario: Publish interval disabled
- **WHEN** both the energy latch and adaptive gradient danger condition are active
- **AND** `spectral_min_publish_interval_ms` is 0
- **THEN** the detector SHALL allow publication for every active FFT window

### Requirement: Spectral detector SHALL not introduce a warning output
The spectral detector SHALL route only the combined temp acoustic-emission publish condition as `FailureEvent::AcousticEmission` and SHALL NOT drive a dedicated status GPIO or create a warning event type.

#### Scenario: Prototype warning condition occurs
- **WHEN** the condition corresponding to `peringatan_retak` occurs
- **THEN** the firmware SHALL update internal latch diagnostics
- **AND** the firmware SHALL NOT publish a warning event type for acoustic emission
- **AND** the firmware SHALL NOT publish `FailureEvent::AcousticEmission` unless adaptive gradient danger is also active and the publish interval allows it
- **AND** the firmware SHALL NOT drive the Arduino prototype status pin

#### Scenario: Prototype danger condition occurs while latch is active
- **WHEN** the condition corresponding to `status_patah` occurs
- **AND** the energy-jump latch is active
- **THEN** the firmware SHALL publish `FailureEvent::AcousticEmission`
- **AND** the existing logger failure pipeline SHALL handle SD storage and MQTT outbox publication
