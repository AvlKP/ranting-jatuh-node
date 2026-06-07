## 1. Restructure PushSample Critical Section

- [x] 1.1 Add `should_compute`, `pub_state`, `is_exit` local variables before the lock_guard scope
- [x] 1.2 In DISTURBED exit transition path (line 385), replace `ComputeAndPublish(NodeState::DISTURBED, true)` call with flag assignments: `should_compute = true; pub_state = NodeState::DISTURBED; is_exit = true;`
- [x] 1.3 In DISTURBED buffer refresh path (lines 391–406), move `ComputeAndPublish(NodeState::DISTURBED, false)` call to after the buffer reset block, capturing the flag via `should_compute = true; pub_state = NodeState::DISTURBED; is_exit = false;` inside the critical section
- [x] 1.4 Add the deferred call `if (should_compute) { ComputeAndPublish(pub_state, is_exit); }` after the lock_guard closing brace
- [x] 1.5 Verify no other `ComputeAndPublish` calls remain inside the mutex scope

## 2. Build and Verify

- [x] 2.1 Build the project (`idf.py build`) and confirm zero errors
- [x] 2.1a Fix stack overflow: `ComputeAndPublish` created two ~12KB `ModalAxisResult` on stack, overflowing 8KB task stack. Refactored `AnalyzeModalAxis` to take output parameter `(data, out)`. Added `roll_modal_scratch_` and `pitch_modal_scratch_` member variables (~24KB persistent RAM). Updated test.
- [x] 2.2 Flash to device and monitor serial output for assertion-free runtime
- [x] 2.3 Verify DISTURBED entry/exit transitions produce correct MonitorResult events
- [x] 2.4 Verify buffer refresh path produces correct events with proper buffer state
