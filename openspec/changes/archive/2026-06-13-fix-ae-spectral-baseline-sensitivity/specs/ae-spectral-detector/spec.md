## MODIFIED Requirements

### Requirement: Energy jump latch SHALL track state without publishing failure events
The detector SHALL track the energy-jump latch state internally for diagnostics but SHALL NOT publish `FailureEvent::AcousticEmission` when the latch activates.

#### Scenario: Energy jump exceeds threshold
- **WHEN** current high-frequency energy minus previous high-frequency energy exceeds the configured jump threshold
- **THEN** the detector SHALL mark the latch active for the configured latch duration
- **AND** the detector SHALL NOT publish a failure event

#### Scenario: Latch expires
- **WHEN** the detector latch is active
- **AND** the configured latch duration has elapsed without a new energy jump
- **THEN** the detector SHALL clear the latch state
- **AND** it SHALL NOT require any status output GPIO change

### Requirement: Adaptive gradient danger SHALL publish acoustic emission failure
The detector SHALL implement the prototype leaking integrator, gradient buffer, EWMA statistics, and dynamic danger threshold, and SHALL treat danger detection as an acoustic emission failure.

#### Scenario: Gradient exceeds dynamic danger threshold
- **WHEN** the leaking integrator gradient is greater than `mu_grad + (K_BAHAYA * sigma_grad)`
- **AND** the gradient has been clamped to a minimum of zero
- **AND** the gradient buffer is full
- **THEN** the detector SHALL publish `FailureEvent::AcousticEmission` through the existing monitor failure event pipeline

#### Scenario: Normal gradient adapts baseline
- **WHEN** the gradient z-score is less than or equal to 3.0
- **THEN** the detector SHALL update EWMA mean using `mu = (alpha * grad) + ((1 - alpha) * mu)`
- **AND** the detector SHALL update EWMA variance using `var = (alpha * diff^2) + ((1 - alpha) * var)` matching the Arduino prototype formula
- **AND** sigma SHALL be clamped to at least 0.1
- **AND** the initial EWMA variance SHALL be 100.0 (sigma = 10.0) matching the Arduino prototype default

#### Scenario: Negative gradient is zeroed before detection
- **WHEN** the computed leaking-integrator gradient is negative
- **THEN** the gradient SHALL be clamped to zero before EWMA baseline adaptation and before danger threshold comparison
