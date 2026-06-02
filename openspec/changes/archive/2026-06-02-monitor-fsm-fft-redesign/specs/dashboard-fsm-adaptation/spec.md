## MODIFIED Requirements

### Requirement: Dashboard renders aperiodic data
The dashboard SHALL correctly display event-driven data from all three FSM states (`IDLE`, `DISTURBED`, `FREE_DECAY`) instead of expecting continuous periodic streams.

#### Scenario: Displaying idle periods
- **WHEN** no new parameter data is received for an extended period
- **THEN** the dashboard indicates the node is in `IDLE` state

#### Scenario: Updating on disturbance
- **WHEN** new parameter data arrives with `state` field `"DISTURBED"`
- **THEN** the dashboard updates its sway graphs and views immediately and indicates `DISTURBED` state

#### Scenario: Displaying FREE_DECAY state
- **WHEN** new parameter data arrives with `state` field `"FREE_DECAY"`
- **THEN** the dashboard SHALL indicate the node is in `FREE_DECAY` state
- **THEN** the dashboard SHALL display the natural frequency and damping ratio results

## ADDED Requirements

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
