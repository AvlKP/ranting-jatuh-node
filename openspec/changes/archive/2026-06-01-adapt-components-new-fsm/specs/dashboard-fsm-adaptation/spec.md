## ADDED Requirements

### Requirement: Dashboard renders aperiodic data
The dashboard SHALL correctly display event-driven data instead of expecting continuous periodic streams.

#### Scenario: Displaying idle periods
- **WHEN** no new parameter data is received for an extended period
- **THEN** the dashboard indicates the node is in `IDLE` state

#### Scenario: Updating on disturbance
- **WHEN** new parameter data arrives
- **THEN** the dashboard updates its graphs and views immediately and indicates `DISTURBED` state
