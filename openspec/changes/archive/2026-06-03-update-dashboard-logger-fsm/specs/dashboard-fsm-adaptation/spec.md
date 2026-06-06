## MODIFIED Requirements

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
