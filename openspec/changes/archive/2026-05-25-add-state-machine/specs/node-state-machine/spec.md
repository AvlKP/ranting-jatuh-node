## ADDED Requirements

### Requirement: Node State Machine Initialization
The node SHALL initialize in the `IDLE` state upon startup, maintaining a short variance buffer.
- `short_buffer_size`, `K_IDLE`, `K_DISTURBED`, and `N_DPAD` SHALL be configured via Kconfig.
- Kconfig parameters for floats (e.g., `K_IDLE`, `K_DISTURBED`) SHALL be implemented as scaled integers to comply with ESP-IDF Kconfig limitations.

#### Scenario: System Startup
- **WHEN** the IoT node is powered on and monitoring starts
- **THEN** the system enters the `IDLE` state
- **THEN** it calculates only tilt statistics (mean, variance) per 5-minute window
- **THEN** it pre-allocates a compile-time fixed-size `std::array` for the short buffer

### Requirement: Live Variance Calculation
The node SHALL calculate variance live in the short buffer on every new sample to evaluate state transitions immediately.
- The live variance calculation MUST be an O(1) rolling algorithm (maintaining sum and sum-of-squares) to ensure deterministic execution time per sample.

### Requirement: Transition to DISTURBED
The node SHALL transition to the `DISTURBED` state based on short-term variance, provided a baseline variance is available.

#### Scenario: Variance Exceeds Threshold
- **WHEN** the node is in the `IDLE` state
- **WHEN** a valid previous 5-minute window variance is available
- **WHEN** the live variance of the short buffer exceeds the previous 5-minute window variance by the scaled `K_IDLE` factor
- **THEN** the state transitions to `DISTURBED`
- **THEN** the short buffer is appended (via `std::copy` or loop, without dynamic allocation) to the 5-minute fixed-size ring buffer

#### Scenario: Missing Baseline Variance (Cold Start Edge Case)
- **WHEN** the node is in the `IDLE` state
- **WHEN** the previous 5-minute window variance is NOT yet available (e.g., initial 5 minutes of operation)
- **THEN** the node SHALL NOT transition to `DISTURBED`, regardless of short buffer variance
- **THEN** the node SHALL wait until the first 5-minute window completes to establish the baseline variance

### Requirement: Parameter Calculation and Return to IDLE
The node SHALL perform heavy calculations and send data immediately upon leaving the `DISTURBED` state.

#### Scenario: Disturbance Subsides
- **WHEN** the node is in the `DISTURBED` state
- **WHEN** the live variance of the short buffer falls below the previous `IDLE` state 5-minute window variance by the scaled `K_DISTURBED` factor
- **THEN** the state transitions back to `IDLE`
- **THEN** the natural frequency, damping ratio, SAMEAN, and SAMAX are calculated from the `DISTURBED` 5-minute buffer
- **THEN** the buffer results are sent immediately (without waiting for the 5-minute interval)

### Requirement: DISTURBED Buffer Refresh
The node SHALL prevent buffer overflow during prolonged disturbances by refreshing the state and computing intermediate parameters.

#### Scenario: Buffer Nearing Capacity
- **WHEN** the node is in the `DISTURBED` state
- **WHEN** the 5-minute buffer is `N_DPAD` away from being full
- **THEN** the natural frequency, damping ratio, SAMEAN, and SAMAX are calculated from the current `DISTURBED` buffer and sent immediately
- **THEN** the node immediately transitions to `DISTURBED` again to refresh the buffer
