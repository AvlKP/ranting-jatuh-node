## ADDED Requirements

### Requirement: Centerline Modal Analysis
The notebook analysis SHALL provide a centerline modal analysis mode that estimates and subtracts a moving baseline from disturbed roll and pitch signals before computing natural frequency.

#### Scenario: Baseline-robust residual
- **WHEN** a disturbed segment contains alternating extrema
- **THEN** the analysis SHALL compute a centerline from adjacent peak/trough pairs
- **THEN** the analysis SHALL compute a residual signal as raw tilt minus interpolated centerline

#### Scenario: Insufficient extrema
- **WHEN** a disturbed segment has fewer than two valid alternating extrema
- **THEN** the analysis SHALL return the raw signal mean as the centerline
- **THEN** damping ratio SHALL be set to 0.0 for that axis

### Requirement: Bounded Natural Frequency Search
The notebook analysis SHALL select natural frequency only from PSD bins within a configurable search band.

#### Scenario: Current log analysis
- **WHEN** natural frequency is computed for current pull-and-release logs
- **THEN** the default FFT search band SHALL be 0.5 Hz to 12.0 Hz
- **THEN** bins outside the search band SHALL NOT be selected as the natural frequency

#### Scenario: No bins in band
- **WHEN** the requested search band contains no PSD bins
- **THEN** natural frequency SHALL be 0.0
- **THEN** damping ratio SHALL be 0.0 for that axis

### Requirement: Half Peak-to-Peak Damping Envelope
The notebook analysis SHALL compute damping regression from half peak-to-peak amplitudes derived from adjacent opposite extrema.

#### Scenario: Valid decay envelope
- **WHEN** at least four half peak-to-peak amplitudes are available and natural frequency is positive
- **THEN** damping ratio SHALL be computed by linear regression of ln(amplitude) versus time

#### Scenario: Too few amplitudes
- **WHEN** fewer than four half peak-to-peak amplitudes are available
- **THEN** damping ratio SHALL be 0.0

### Requirement: Notebook Disturbance Cutoff
The notebook disturbance detector SHALL use 0.2 Hz as the default Chebyshev HPF cutoff for this analysis workflow.

#### Scenario: Default disturbance detection
- **WHEN** disturbance detection is called without an explicit cutoff
- **THEN** the HPF cutoff SHALL be 0.2 Hz
