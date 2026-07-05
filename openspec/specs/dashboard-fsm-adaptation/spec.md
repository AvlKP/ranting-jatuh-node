# dashboard-fsm-adaptation Specification

## Purpose
TBD - created by archiving change adapt-components-new-fsm. Update Purpose after archive.
## Requirements
### Requirement: Dashboard renders aperiodic data
The dashboard SHALL correctly display event-driven data from all three FSM states (`IDLE`, `DISTURBED`, `FREE_DECAY`) instead of expecting continuous periodic streams. The UI SHALL visually indicate the current state.

#### Scenario: Displaying idle periods
- **WHEN** no new parameter data is received for an extended period
- **THEN** the dashboard indicates the node is in `IDLE` state

#### Scenario: Updating on disturbance
- **WHEN** new parameter data arrives with `state` field `"DISTURBED"`
- **THEN** the dashboard updates its sway graphs and views immediately and indicates `DISTURBED` state

#### Scenario: Displaying FREE_DECAY state
- **WHEN** new parameter data arrives with `state` field `"FREE_DECAY"`
- **THEN** the dashboard SHALL indicate the node is in `FREE_DECAY` state visually in the UI
- **THEN** the dashboard SHALL display the natural frequency and damping ratio results

### Requirement: Dashboard displays per-axis natural frequency
The dashboard SHALL display `natural_freq_roll_hz` and `natural_freq_pitch_hz` as separate values instead of a single combined natural frequency.

#### Scenario: Per-axis frequency display
- **WHEN** the dashboard receives a FREE_DECAY parameter payload
- **THEN** the dashboard SHALL display `natural_freq_roll_hz` with axis label
- **THEN** the dashboard SHALL display `natural_freq_pitch_hz` with axis label

#### Scenario: Per-axis damping display
- **WHEN** the dashboard receives a FREE_DECAY parameter payload
- **THEN** the dashboard SHALL display `damping_ratio_roll` with axis label
- **THEN** the dashboard SHALL display `damping_ratio_pitch` with axis label

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
