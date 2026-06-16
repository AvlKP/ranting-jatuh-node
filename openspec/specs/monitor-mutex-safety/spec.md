# monitor-mutex-safety Specification

## Purpose
Ensure mutex-protected monitor state transitions and snapshot APIs are concurrency-safe on ESP32-S3 SMP without holding locks across slow operations (I/O, logging, allocation, ESP event posting).
## Requirements
### Requirement: PushSample SHALL NOT hold mutex across ComputeAndPublish

The `Monitor::PushSample` function SHALL release `mutex_` before invoking `ComputeAndPublish`. The critical section protected by `mutex_` SHALL be bounded to buffer read/write operations and state variable updates only.

#### Scenario: Normal IDLE path does not call ComputeAndPublish

- **WHEN** `PushSample` is called and `state_` is `IDLE` and `hpf_magnitude` is below the HPF threshold
- **THEN** `mutex_` SHALL be released before `PushSample` returns
- **AND** `ComputeAndPublish` SHALL NOT be called

#### Scenario: IDLE to DISTURBED transition does not call ComputeAndPublish

- **WHEN** `PushSample` is called and `state_` is `IDLE` and `hpf_magnitude` exceeds the HPF threshold
- **THEN** the state transition to `DISTURBED` and buffer copy from short history SHALL complete under `mutex_` protection
- **THEN** `mutex_` SHALL be released before `PushSample` returns
- **AND** `ComputeAndPublish` SHALL NOT be called

#### Scenario: DISTURBED exit transition defers ComputeAndPublish

- **WHEN** `PushSample` is called and `state_` is `DISTURBED` and the HPF magnitude drops below threshold for enough consecutive samples to trigger the exit debounce
- **THEN** the decision to call `ComputeAndPublish(state=DISTURBED, is_exit=true)` SHALL be captured as local variables inside the critical section
- **THEN** the state transition to `IDLE` SHALL complete under `mutex_` protection
- **THEN** `mutex_` SHALL be released
- **AND** `ComputeAndPublish` SHALL be called AFTER the lock_guard destructor completes

#### Scenario: DISTURBED buffer refresh defers ComputeAndPublish

- **WHEN** `PushSample` is called and `state_` is `DISTURBED` and no exit transition occurs and `sample_count_` reaches the threshold for a buffer refresh
- **THEN** the decision to call `ComputeAndPublish(state=DISTURBED, is_exit=false)` SHALL be captured as local variables inside the critical section
- **THEN** the buffer reset (`write_index_ = 0`, `sample_count_ = 0`, history copy from short buffer) SHALL complete under `mutex_` protection
- **THEN** `mutex_` SHALL be released
- **AND** `ComputeAndPublish` SHALL be called AFTER the lock_guard destructor completes

### Requirement: Mutex unlock SHALL NOT trigger FreeRTOS assertion

The system SHALL NOT panic or assert during `pthread_mutex_unlock` when `PushSample` returns and the `std::lock_guard<std::mutex>` destructor executes.

#### Scenario: PushSample returns without assertion failure

- **WHEN** `PushSample` completes execution (regardless of internal code path)
- **THEN** the lock_guard destructor SHALL unlock `mutex_` successfully
- **AND** no `__assert_func` or `configASSERT` failure SHALL occur in `pthread_mutex_unlock`, `xQueueGenericSend`, `prvCopyDataToQueue`, or `xTaskPriorityDisinherit`

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
