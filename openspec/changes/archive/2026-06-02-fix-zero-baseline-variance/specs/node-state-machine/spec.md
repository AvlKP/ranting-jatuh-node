## MODIFIED Requirements

### Requirement: Transition to DISTURBED
The node SHALL transition to the `DISTURBED` state based on short-term variance, provided a baseline variance is available. The transition threshold SHALL be the greater of the relative threshold (`baseline * K_IDLE`) and the absolute minimum variance floor (`K_ABS_MIN_VAR`).

#### Scenario: Variance Exceeds Threshold
- **WHEN** the node is in the `IDLE` state
- **WHEN** a valid previous 5-minute window variance is available
- **WHEN** the live variance of the short buffer exceeds `max(baseline * K_IDLE, K_ABS_MIN_VAR)`
- **THEN** the state transitions to `DISTURBED`
- **THEN** the short buffer is appended (via `std::copy` or loop, without dynamic allocation) to the 5-minute fixed-size ring buffer

#### Scenario: Variance Below Absolute Floor Despite Relative Threshold
- **WHEN** the node is in the `IDLE` state
- **WHEN** baseline variance is near-zero (e.g., device perfectly still for extended period)
- **WHEN** the live short buffer variance exceeds `baseline * K_IDLE` but is below `K_ABS_MIN_VAR`
- **THEN** the node SHALL NOT transition to `DISTURBED`
- **THEN** the node remains in `IDLE`

### Requirement: Parameter Calculation and Return to IDLE
The node SHALL perform heavy calculations and send data immediately upon leaving the `DISTURBED` state. The return threshold SHALL be the greater of the relative threshold (`baseline * K_DISTURBED`) and the absolute minimum variance floor (`K_ABS_MIN_VAR`).

#### Scenario: Disturbance Subsides
- **WHEN** the node is in the `DISTURBED` state
- **WHEN** the live variance of the short buffer falls below `max(baseline * K_DISTURBED, K_ABS_MIN_VAR)`
- **THEN** the state transitions back to `IDLE`
- **THEN** the natural frequency, damping ratio, SAMEAN, and SAMAX are calculated from the `DISTURBED` 5-minute buffer
- **THEN** the buffer results are sent immediately (without waiting for the 5-minute interval)

#### Scenario: Return to IDLE With Near-Zero Baseline
- **WHEN** the node is in the `DISTURBED` state
- **WHEN** baseline variance is near-zero
- **WHEN** the live short buffer variance falls below `K_ABS_MIN_VAR`
- **THEN** the state transitions back to `IDLE` (floor provides an achievable threshold)

## ADDED Requirements

### Requirement: Absolute Minimum Variance Configuration
The absolute minimum variance floor (`K_ABS_MIN_VAR`) SHALL be configurable via Kconfig as a scaled integer (`MONITOR_ABS_MIN_VAR_X10000`).
- The default value SHALL be 50 (representing 0.005 deg²).
- The value SHALL apply equally to both roll and pitch variance comparisons.
- The Kconfig parameter SHALL use the x10000 scaling convention consistent with existing scaled parameters.

#### Scenario: Configuration Applied at Runtime
- **WHEN** the monitor initializes
- **THEN** the absolute minimum variance floor is computed as `MONITOR_ABS_MIN_VAR_X10000 / 10000.0f`
- **THEN** this value is used in all state transition threshold comparisons
