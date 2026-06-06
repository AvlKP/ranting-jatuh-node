# dashboard-fft-display Specification

## Purpose
The dashboard FFT chart displays the power spectral density of the most recent disturbance analysis event for visual inspection of frequency content.

## Requirements

### Requirement: Per-Event FFT Display
The dashboard FFT chart SHALL display the power spectral density of the most recent DISTURBED→IDLE analysis event only, not a cumulative sum across multiple events.

#### Scenario: First analysis event
- **WHEN** the first DISTURBED→IDLE transition occurs and FFT is computed
- **THEN** the dashboard SHALL display the PSD from that event

#### Scenario: Subsequent analysis event
- **WHEN** a second DISTURBED→IDLE transition occurs with different frequency content
- **THEN** the dashboard SHALL display the PSD from the second event only
- **THEN** the first event's PSD SHALL NOT be visible in the chart

#### Scenario: No analysis event yet
- **WHEN** no DISTURBED→IDLE transition has occurred since boot
- **THEN** the dashboard FFT chart SHALL display the default zero-initialized PSD array

### Requirement: Accumulator Reset
The `psd_accum_` array SHALL be zeroed at the start of each DISTURBED→IDLE analysis event in `ComputeAndPublish()`.

#### Scenario: Reset before FFT computation
- **WHEN** `ComputeAndPublish(is_exit=true)` is called for the DISTURBED state
- **THEN** `psd_accum_` SHALL be filled with zeros before any FFT computation begins
- **THEN** Welch averaging within the event SHALL accumulate into a clean accumulator

#### Scenario: Welch averaging preserved
- **WHEN** `ComputeAxisNaturalFrequency()` processes multiple FFT segments within a single analysis event
- **THEN** `psd_accum_` SHALL accumulate PSD across all segments within that event
- **THEN** the final result SHALL represent the averaged PSD of that single event
