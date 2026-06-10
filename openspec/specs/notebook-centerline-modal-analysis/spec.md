# notebook-centerline-modal-analysis Specification

## Purpose
Notebook workflow for baseline-robust modal analysis of pull-and-release IMU logs.
## Requirements
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
The notebook analysis SHALL compute damping regression from half peak-to-peak amplitudes derived from adjacent opposite collapsed extrema.

#### Scenario: Valid decay envelope
- **WHEN** at least four half peak-to-peak amplitudes from collapsed extrema are available and natural frequency is positive
- **THEN** damping ratio SHALL be computed by linear regression of ln(amplitude) versus time

#### Scenario: Too few amplitudes
- **WHEN** fewer than four half peak-to-peak amplitudes from collapsed extrema are available
- **THEN** damping ratio SHALL be 0.0

### Requirement: Notebook Disturbance Cutoff
The notebook disturbance detector SHALL use 0.2 Hz as the default Chebyshev HPF cutoff for this analysis workflow.

#### Scenario: Default disturbance detection
- **WHEN** disturbance detection is called without an explicit cutoff
- **THEN** the HPF cutoff SHALL be 0.2 Hz

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

