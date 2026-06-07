## ADDED Requirements

### Requirement: Lobe-Collapsed Extrema Selection
The notebook analysis SHALL collapse raw local extrema into one representative extrema per physical peak or trough lobe before centerline pair construction.

#### Scenario: Multiple maxima inside one peak lobe
- **WHEN** a peak lobe contains multiple local maxima before a valid reversal into the next trough lobe
- **THEN** the analysis SHALL retain the strongest maximum as the representative peak extrema
- **THEN** weaker maxima in that same lobe SHALL NOT create separate centerline pairs

#### Scenario: Multiple minima inside one trough lobe
- **WHEN** a trough lobe contains multiple local minima before a valid reversal into the next peak lobe
- **THEN** the analysis SHALL retain the strongest minimum as the representative trough extrema
- **THEN** weaker minima in that same lobe SHALL NOT create separate centerline pairs

#### Scenario: Reversal too small
- **WHEN** an opposite-kind local extrema differs from the active lobe representative by less than the configured lobe reversal threshold
- **THEN** the analysis SHALL treat that extrema as intra-lobe ripple
- **THEN** the analysis SHALL NOT finalize the current lobe because of that extrema

#### Scenario: Raw extrema diagnostics preserved
- **WHEN** lobe collapse is applied
- **THEN** the analysis SHALL expose raw extrema diagnostics
- **THEN** the analysis SHALL expose collapsed extrema diagnostics

## MODIFIED Requirements

### Requirement: Centerline Modal Analysis
The notebook analysis SHALL provide a centerline modal analysis mode that estimates and subtracts a moving baseline from disturbed roll and pitch signals before computing natural frequency. The centerline SHALL be computed from adjacent peak/trough pairs after raw local extrema are collapsed by lobe.

#### Scenario: Baseline-robust residual
- **WHEN** a disturbed segment contains collapsed alternating extrema
- **THEN** the analysis SHALL compute a centerline from adjacent collapsed peak/trough pairs
- **THEN** the analysis SHALL compute a residual signal as raw tilt minus interpolated centerline

#### Scenario: Insufficient extrema
- **WHEN** a disturbed segment has fewer than two valid collapsed alternating extrema
- **THEN** the analysis SHALL return the raw signal mean as the centerline
- **THEN** damping ratio SHALL be set to 0.0 for that axis

### Requirement: Half Peak-to-Peak Damping Envelope
The notebook analysis SHALL compute damping regression from half peak-to-peak amplitudes derived from adjacent opposite collapsed extrema.

#### Scenario: Valid decay envelope
- **WHEN** at least four half peak-to-peak amplitudes from collapsed extrema are available and natural frequency is positive
- **THEN** damping ratio SHALL be computed by linear regression of ln(amplitude) versus time

#### Scenario: Too few amplitudes
- **WHEN** fewer than four half peak-to-peak amplitudes from collapsed extrema are available
- **THEN** damping ratio SHALL be 0.0
