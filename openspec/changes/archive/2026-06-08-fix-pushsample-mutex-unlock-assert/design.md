## Context

`Monitor::PushSample` (monitor.cpp:302) acquires `mutex_` via `std::lock_guard` at entry and holds it through the entire function body, including calls to `ComputeAndPublish` at lines 385 and 392. `ComputeAndPublish` performs:

- `ComputeStats` — reads `roll_history_`/`pitch_history_`/`sample_count_`/`write_index_`
- `ComputeSwayAndDamping` — statistical computation
- `AnalyzeModalAxis` × 2 — FFT-based modal analysis with DSP heap allocations
- `esp_event_post` — posts `MONITOR_EVENT_RESULT` to the default event loop

The event loop task or other tasks (logger, dashboard) that handle `MONITOR_EVENT_RESULT` may call `GetFftData`, `GetTiltHistory`, or `GetLatestSamples`, all of which contend for `mutex_`. When a higher-priority task blocks on the mutex while `PushSample` still holds it inside `ComputeAndPublish`, FreeRTOS engages priority inheritance. When `PushSample` returns and the lock_guard destructor unlocks, `pthread_mutex_unlock` → `xTaskPriorityDisinherit` hits an internal `configASSERT` due to mutex state inconsistency after the prolonged critical section.

## Goals / Non-Goals

**Goals:**
- `mutex_` critical section in `PushSample` SHALL cover only buffer read/write operations
- `ComputeAndPublish` SHALL be called after `mutex_` is released
- No change to public API, state machine logic, or event posting behavior

**Non-Goals:**
- Changing the mutex type (e.g., recursive mutex)
- Splitting `mutex_` into multiple granular locks
- Modifying `ComputeAndPublish` or `ComputeStats` internals
- Thread-safety changes beyond `PushSample`

## Decisions

**Decision 1: Deferred execution via boolean flag**

Capture the decision to call `ComputeAndPublish` (and its arguments `pub_state`, `is_exit`) as local variables inside the critical section, then execute the call after the `lock_guard` scope exits.

```cpp
void Monitor::PushSample(float roll, float pitch, float hpf_magnitude) noexcept {
    bool should_compute = false;
    NodeState pub_state = NodeState::IDLE;
    bool is_exit = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        // ... all current buffer manipulation, state transitions ...
        // Instead of calling ComputeAndPublish(...) directly, set flags:
        should_compute = true;
        pub_state = NodeState::DISTURBED;
        is_exit = true;  // or false
    } // mutex_ released here

    if (should_compute) {
        ComputeAndPublish(pub_state, is_exit);
    }
}
```

**Alternatives considered:**

| Alternative | Verdict | Rationale |
|---|---|---|
| Recursive `std::recursive_mutex` | Rejected | Masks the root cause. Doesn't fix the problem — deadlocks and priority inversion still possible but silently. |
| Split `mutex_` into `data_mutex_` + `compute_mutex_` | Rejected | Overcomplicates for a single-mutex design. No other callers hold the mutex across long operations. |
| Move `ComputeAndPublish` to a separate task via queue | Rejected | Adds unnecessary FreeRTOS queue, increases latency, over-engineered for this fix. |
| Deferred flag approach (chosen) | Accepted | Minimal diff, zero overhead, no new synchronization primitives. Keeps buffer atomicity intact. |

**Decision 2: Guard DISTURBED buffer refresh atomically**

The DISTURBED buffer refresh path (lines 391–406) both calls `ComputeAndPublish` AND resets `write_index_`/`sample_count_`. The reset must remain inside the critical section to prevent a concurrent reader from seeing an inconsistent state. Only the `ComputeAndPublish` call is deferred.

## Risks / Trade-offs

- **Timing shift**: `ComputeAndPublish` now runs after buffer manipulation completes rather than during. This is actually correct behavior — the computed result reflects the buffer state at the moment of the decision, not a half-updated buffer.
- **Double-compute**: In the DISTURBED→IDLE exit path (line 385), the state transition flag `transitioned` is true, so the buffer refresh path at line 391 is skipped. No risk of computing twice.
- **Re-entrancy**: `PushSample` is called from `Monitor::Update` (line 229) in a single-producer loop. No concurrent `PushSample` calls exist. Safe.
