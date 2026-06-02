## ADDED Requirements

### Requirement: Adaptive FFT window sizing
The monitor SHALL select the FFT window size based on available sample count rather than requiring a fixed 1024-sample minimum.

#### Scenario: Very short disturbance (fewer than 512 samples)
- **WHEN** `ComputeNaturalFrequency()` is called with `BufferSize()` > 0 and < 512
- **THEN** the system SHALL zero-pad the buffer to 512 samples
- **THEN** the system SHALL compute a single 512-point FFT with a Hann window applied to the real samples only
- **THEN** the system SHALL write the resulting 256 PSD bins into `psd_accum_` by duplicating each bin into two consecutive slots (slot `2i` and `2i+1`)

#### Scenario: Medium disturbance (512 to 1023 samples)
- **WHEN** `ComputeNaturalFrequency()` is called with `BufferSize()` >= 512 and < 1024
- **THEN** the system SHALL zero-pad the buffer to 1024 samples
- **THEN** the system SHALL compute a single 1024-point FFT with a Hann window applied to the real samples only
- **THEN** the system SHALL write the resulting 512 PSD bins directly into `psd_accum_`

#### Scenario: Long disturbance (1024 or more samples)
- **WHEN** `ComputeNaturalFrequency()` is called with `BufferSize()` >= 1024
- **THEN** the system SHALL use the existing Welch averaging method with 1024-point windows and 50% overlap (behavior unchanged)

### Requirement: Empty buffer guard
The system SHALL zero `psd_accum_` and return false only when `BufferSize()` is zero.

#### Scenario: No samples available
- **WHEN** `ComputeNaturalFrequency()` is called with `BufferSize()` == 0
- **THEN** `psd_accum_` SHALL be filled with 0.0f
- **THEN** `result.natural_freq_hz` SHALL be set to 0.0f
- **THEN** the function SHALL return false

### Requirement: Peak frequency from adaptive FFT
The system SHALL compute `natural_freq_hz` from the peak PSD bin, adjusting the frequency calculation for the actual FFT size used.

#### Scenario: Peak frequency with 512-point FFT
- **WHEN** a 512-point FFT is computed
- **THEN** `natural_freq_hz` SHALL equal `(max_bin * sample_rate) / 512`

#### Scenario: Peak frequency with 1024-point FFT
- **WHEN** a 1024-point FFT is computed (single window or Welch)
- **THEN** `natural_freq_hz` SHALL equal `(max_bin * sample_rate) / 1024`
