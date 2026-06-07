## MODIFIED Requirements

### Requirement: Peak Envelope Tracking

The system SHALL track lobe-collapsed peak/trough pair amplitudes within the identified decay region for use by the centerline and damping estimation algorithm. Raw local extrema SHALL be reduced to one representative extrema per physical peak or trough lobe before pair construction.

#### Scenario: Sufficient centerline pairs found

- **WHEN** the decay region contains at least four valid adjacent opposite lobe-collapsed extrema pairs meeting amplitude, spacing, and lobe reversal criteria
- **THEN** half peak-to-peak envelope data (pair amplitudes and pair times) SHALL be passed to the damping estimator
- **THEN** centerline pair data SHALL be passed to residual generation

#### Scenario: Insufficient centerline pairs

- **WHEN** the decay region contains fewer than four valid adjacent opposite lobe-collapsed extrema pairs
- **THEN** damping ratio SHALL be set to 0.0
- **THEN** natural frequency SHALL still be computed only if at least two valid centerline pairs exist for residual generation

### Requirement: Configurable Peak Detection Parameters

PEAK_MIN_SPACING and PEAK_MIN_AMPLITUDE SHALL be configurable via Kconfig. CENTERLINE_MIN_AMPLITUDE and CENTERLINE_LOBE_REVERSAL SHALL also be configurable via Kconfig for modal analysis.

- PEAK_MIN_SPACING default SHALL be 2 samples (supports up to ~6.5 Hz at 26 Hz ODR).
- PEAK_MIN_AMPLITUDE default SHALL be 0.5 degrees. In Kconfig this SHALL be represented as `CONFIG_MONITOR_PEAK_MIN_AMPLITUDE_X10` with default value 5 (representing 0.5°), to avoid floating-point in Kconfig.
- CENTERLINE_MIN_AMPLITUDE default SHALL be 0.05 degrees. In Kconfig this SHALL be represented as `CONFIG_MONITOR_CENTERLINE_MIN_AMPLITUDE_X100` with default value 5 (representing 0.05°).
- CENTERLINE_LOBE_REVERSAL default SHALL be 0.10 degrees. In Kconfig this SHALL be represented as `CONFIG_MONITOR_CENTERLINE_LOBE_REVERSAL_X100` with default value 10 (representing 0.10°).

#### Scenario: Sway peak parameter applied

- **WHEN** sway peak detection runs with PEAK_MIN_AMPLITUDE = 0.5° (CONFIG_MONITOR_PEAK_MIN_AMPLITUDE_X10 = 5)
- **THEN** peaks with absolute amplitude below 0.5° SHALL be ignored and excluded from sway statistics

#### Scenario: Centerline pair amplitude parameter applied

- **WHEN** centerline pair construction runs with CENTERLINE_MIN_AMPLITUDE = 0.05° (CONFIG_MONITOR_CENTERLINE_MIN_AMPLITUDE_X100 = 5)
- **THEN** adjacent collapsed extrema pairs with half peak-to-peak amplitude below 0.05° SHALL be excluded from centerline and damping estimation

#### Scenario: Lobe reversal parameter applied

- **WHEN** lobe collapse evaluates an opposite-kind local extrema whose distance from the active lobe representative is below CENTERLINE_LOBE_REVERSAL
- **THEN** the opposite-kind extrema SHALL be treated as intra-lobe ripple
- **THEN** the active lobe SHALL NOT be finalized by that extrema

## ADDED Requirements

### Requirement: Lobe-Collapsed Firmware Extrema Selection
The monitor SHALL collapse raw local extrema into one representative extrema per physical peak or trough lobe before centerline pair construction.

#### Scenario: Multiple maxima inside one peak lobe
- **WHEN** a peak lobe contains multiple local maxima before a valid reversal into the next trough lobe
- **THEN** the monitor SHALL retain the strongest maximum as the representative peak extrema
- **THEN** weaker maxima in that same lobe SHALL NOT create separate centerline pairs

#### Scenario: Multiple minima inside one trough lobe
- **WHEN** a trough lobe contains multiple local minima before a valid reversal into the next peak lobe
- **THEN** the monitor SHALL retain the strongest minimum as the representative trough extrema
- **THEN** weaker minima in that same lobe SHALL NOT create separate centerline pairs

#### Scenario: Alternating collapsed extrema preserved
- **WHEN** lobe collapse completes for a decay region
- **THEN** emitted collapsed extrema SHALL alternate between peak and trough kinds
- **THEN** adjacent emitted extrema SHALL be eligible for centerline pair construction
