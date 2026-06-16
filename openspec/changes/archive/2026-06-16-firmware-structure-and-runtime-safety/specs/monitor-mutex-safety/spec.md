## ADDED Requirements

### Requirement: Monitor snapshot APIs SHALL use bounded critical sections
Monitor APIs that copy history, stream samples, FFT data, state, or diagnostics for dashboard/verification use SHALL protect shared state with bounded critical sections.

#### Scenario: Dashboard copies tilt history
- **WHEN** the dashboard requests tilt history while the monitor task is writing samples
- **THEN** the monitor SHALL copy a bounded number of samples under synchronization
- **AND** it SHALL release synchronization before performing HTTP response formatting

#### Scenario: Dashboard copies latest stream samples
- **WHEN** the dashboard requests latest stream samples while the monitor task is writing a new sample
- **THEN** the monitor SHALL return a consistent bounded snapshot
- **AND** it SHALL NOT expose partially-written `StreamSample` data

### Requirement: Dashboard-visible monitor state SHALL be read safely
Dashboard-visible state and counters SHALL be returned through monitor APIs that are safe across ESP32-S3 cores.

#### Scenario: Dashboard reads node state
- **WHEN** the dashboard status handler reads the monitor node state
- **THEN** the state SHALL be read through a synchronized or atomic API
- **AND** the read SHALL NOT race with monitor state transitions

#### Scenario: Dashboard reads monitor drop counters
- **WHEN** the dashboard status handler reads monitor result or failure drop counters
- **THEN** the counters SHALL be read through atomic loads or a synchronized diagnostic snapshot
- **AND** the read SHALL NOT race with event publication failure paths

### Requirement: Snapshot APIs SHALL not hold monitor lock during slow work
Monitor locks SHALL NOT be held while performing DSP, ESP event posting, SD I/O, MQTT I/O, or HTTP response transmission.

#### Scenario: Monitor publishes result after computation
- **WHEN** a disturbance exit triggers result computation and publication
- **THEN** monitor locks SHALL be released before ESP event posting
- **AND** monitor locks SHALL NOT be held during SD or MQTT work performed by subscribers

#### Scenario: Dashboard status response streams JSON
- **WHEN** the dashboard status handler streams JSON to an HTTP client
- **THEN** it SHALL hold monitor synchronization only while copying snapshots
- **AND** it SHALL release monitor synchronization before calling HTTP send functions
