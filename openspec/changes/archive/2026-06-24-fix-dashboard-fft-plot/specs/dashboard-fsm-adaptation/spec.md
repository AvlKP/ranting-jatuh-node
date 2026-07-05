## ADDED Requirements

### Requirement: Dashboard FFT Plot Reflects Latest Monitor PSD
The dashboard SHALL render FFT plot data from the latest dashboard-visible monitor PSD returned through `/api/status`.

#### Scenario: Status response includes computed PSD
- **WHEN** monitor modal analysis has completed a successful dominant-axis FFT
- **WHEN** the dashboard requests `/api/status`
- **THEN** the `"fft"` array SHALL contain downsampled nonzero PSD values from that FFT
- **THEN** the browser FFT chart SHALL update from those values without requiring a page reload

#### Scenario: No valid PSD available
- **WHEN** no successful dominant-axis FFT has produced PSD data since boot or the latest analysis failed before FFT output was valid
- **WHEN** the dashboard requests `/api/status`
- **THEN** the `"fft"` array SHALL not contain stale nonzero PSD values from an older event

