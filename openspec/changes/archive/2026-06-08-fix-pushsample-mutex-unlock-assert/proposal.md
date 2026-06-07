## Why

`Monitor::PushSample` holds `mutex_` across a call to `ComputeAndPublish`, which performs heavy DSP work (FFT, modal analysis) and posts events via `esp_event_post`. The event post unblocks other tasks that may contend for the same mutex, triggering FreeRTOS priority inheritance. When `PushSample` returns and the lock_guard destructor runs, `pthread_mutex_unlock` hits an assertion in `xTaskPriorityDisinherit` because the mutex state is inconsistent after the prolonged critical section with concurrent waiters. The system panics and reboots in a loop.

## What Changes

- Restructure `PushSample` to release `mutex_` before calling `ComputeAndPublish`
- Buffer state transitions (state_, write_index_, sample_count_, history buffers) remain under mutex protection
- `ComputeAndPublish` calls deferred to after the lock_guard scope exits, using locally captured decision state (pub_state, is_exit)
- No API changes, no behavior changes — purely a critical section reduction

## Capabilities

### New Capabilities

- `monitor-mutex-safety`: Ensure monitor data mutex critical sections are bounded to buffer manipulation only. Long-running operations (FFT, modal analysis, event posting) must execute outside the lock.

### Modified Capabilities

None. This is a bugfix; no requirement-level behavior changes.

## Impact

- Affected code: `components/monitor/monitor.cpp` — `PushSample` function (lines 302–408)
- No API, dependency, or configuration changes
- No breaking changes
