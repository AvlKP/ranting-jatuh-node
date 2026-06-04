# Spec: Envelope Damping Regression

Robust damping ratio estimation via least-squares linear regression on ln(peak amplitudes) vs peak time.

## ADDED Requirements

### Requirement: Envelope Regression Damping

The system SHALL compute damping ratio using least-squares linear regression on ln(|peak_amplitude|) vs peak_time for each axis independently.

Model: A(t) = A₀ × e^(-ζωₙt), therefore ln|A| = ln(A₀) - ζωₙt. The regression slope = -ζωₙ. Given natural frequency ωₙ from FFT: ζ = |slope| / ωₙ.

#### Scenario: Normal decay

- **WHEN** the decay region contains 6+ peaks and FFT yields a valid natural frequency ωₙ > 0
- **THEN** least-squares linear regression SHALL be performed on ln(|peak_amplitude|) vs peak_time, and ζ SHALL be computed as |slope| / ωₙ

#### Scenario: Natural frequency is zero

- **WHEN** FFT fails to produce a valid natural frequency (ωₙ = 0)
- **THEN** damping ratio SHALL be set to 0.0

#### Scenario: Regression slope is positive

- **WHEN** regression yields a positive slope (amplitude increasing, not decaying)
- **THEN** damping ratio SHALL be set to 0.0

### Requirement: Minimum Peak Count

The regression SHALL require a minimum of 4 peaks. With fewer peaks, damping ratio SHALL be 0.0.

#### Scenario: Exactly 3 peaks

- **WHEN** the decay region contains exactly 3 valid peaks
- **THEN** the peak count is insufficient and damping ratio SHALL be set to 0.0

#### Scenario: Exactly 4 peaks

- **WHEN** the decay region contains exactly 4 valid peaks
- **THEN** the minimum peak count is met and regression SHALL be performed

### Requirement: Per-Axis Independence

Roll and pitch damping ratios SHALL be computed independently using their respective peak envelopes and natural frequencies.

#### Scenario: Asymmetric peak availability

- **WHEN** roll axis has sufficient peaks (≥ 4) for regression but pitch axis does not
- **THEN** roll_damping SHALL be computed via regression, and pitch_damping SHALL be set to 0.0
