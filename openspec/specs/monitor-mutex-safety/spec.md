## ADDED Requirements

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
