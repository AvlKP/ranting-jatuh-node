# Spec: Post-Hoc Decay Detection

Retroactive identification of the decay region within the stored DISTURBED buffer and peak envelope extraction for damping estimation.

## ADDED Requirements

### Requirement: Decay Region Identification

On DISTURBED→IDLE transition, the monitor SHALL retroactively identify the decay region in the stored tilt buffer. The decay start is defined as the location of the peak with maximum absolute amplitude. The decay end is the last sample in the buffer.

#### Scenario: Clear impulse response

- **WHEN** a DISTURBED→IDLE transition occurs and the tilt buffer contains a clear impulse response with a distinct maximum peak
- **THEN** the decay region SHALL span from the index of the maximum absolute amplitude peak to the last sample in the buffer

#### Scenario: No peaks found

- **WHEN** a DISTURBED→IDLE transition occurs and no peaks exceed PEAK_MIN_AMPLITUDE (e.g., very small disturbance)
- **THEN** the decay region SHALL be empty, and both frequency and damping ratio SHALL be set to 0.0

#### Scenario: Multiple high peaks from re-excitation

- **WHEN** a DISTURBED→IDLE transition occurs and the buffer contains multiple peaks of equal maximum absolute amplitude (re-excitation within DISTURBED)
- **THEN** the system SHALL use the LAST maximum peak as the decay start

### Requirement: Peak Envelope Tracking

The system SHALL track peak amplitudes (local maxima and minima meeting minimum amplitude and spacing criteria) within the identified decay region for use by the damping estimation algorithm.

#### Scenario: Sufficient peaks found

- **WHEN** the decay region contains at least 4 peaks meeting amplitude and spacing criteria
- **THEN** envelope data (peak amplitudes and their time indices) SHALL be passed to the damping estimator

#### Scenario: Insufficient peaks

- **WHEN** the decay region contains fewer than 4 peaks meeting criteria
- **THEN** damping ratio SHALL be set to 0.0, and natural frequency SHALL still be computed via FFT on the decay region

### Requirement: Configurable Peak Detection Parameters

PEAK_MIN_SPACING and PEAK_MIN_AMPLITUDE SHALL be configurable via Kconfig.

- PEAK_MIN_SPACING default SHALL be 2 samples (supports up to ~6.5 Hz at 26 Hz ODR).
- PEAK_MIN_AMPLITUDE default SHALL be 0.5 degrees. In Kconfig this SHALL be represented as `CONFIG_MONITOR_PEAK_MIN_AMPLITUDE_X10` with default value 5 (representing 0.5°), to avoid floating-point in Kconfig.

#### Scenario: Parameter applied

- **WHEN** peak detection runs with PEAK_MIN_AMPLITUDE = 0.5° (CONFIG_MONITOR_PEAK_MIN_AMPLITUDE_X10 = 5)
- **THEN** peaks with absolute amplitude below 0.5° SHALL be ignored and excluded from the envelope
