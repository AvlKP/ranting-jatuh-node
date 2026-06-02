## 1. Monitor Data Structures & Kconfig

- [x] 1.1 Add new Kconfig parameters: `K_HIGH_X100`, `K_MID_X100`, `K_LOW_X100`, `ACCEL_ERR_SHORT_BUF_SIZE`, `ABS_MIN_ACCEL_VAR_X1000000`, `FREE_DECAY_DEBOUNCE`, `FREE_DECAY_TIMEOUT_S`
- [x] 1.2 Add `FREE_DECAY` to `NodeState` enum in `monitor.hpp`
- [x] 1.3 Update `MonitorResult` struct: add `natural_freq_roll_hz`, `natural_freq_pitch_hz`, `state` field. Keep `natural_freq_hz` for backward compat (set to max of roll/pitch)
- [x] 1.4 Add accel_err short buffer members to `Monitor` class: `accel_err_short_[]`, rolling sum/sq_sum, `accel_err_baseline_var_`, `has_accel_err_baseline_`
- [x] 1.5 Add FREE_DECAY tracking members: `decay_start_index_`, `decay_sample_count_`, `free_decay_debounce_counter_`, `free_decay_entry_time_us_`

## 2. Accelerometer Error Tracking

- [x] 2.1 Implement `ComputeAccelError()`: compute `|√(ax²+ay²+az²) - 1.0f|` from raw IMU accel values in `Update()`
- [x] 2.2 Implement accel_err short buffer push with O(1) rolling variance (same pattern as existing tilt short buffer)
- [x] 2.3 Add accel_err baseline variance accumulation during IDLE 5-minute window completion
- [x] 2.4 Verify LSM6DS3 driver outputs accelerometer values in g units; add unit conversion if needed

## 3. FSM State Transition Logic

- [x] 3.1 Refactor `PushSample()` to accept accel values alongside roll/pitch, compute accel_err_var inline
- [x] 3.2 Replace IDLE→DISTURBED transition: change from tilt variance check to `accel_err_var > max(baseline × K_HIGH, K_ABS_MIN_ACCEL_VAR)`
- [x] 3.3 Implement DISTURBED→FREE_DECAY transition with 128-sample debounce: track consecutive samples below `max(baseline × K_MID, K_ABS_MIN_ACCEL_VAR)`
- [x] 3.4 Implement FREE_DECAY→IDLE transition: `accel_err_var < max(baseline × K_LOW, K_ABS_MIN_ACCEL_VAR)`
- [x] 3.5 Implement FREE_DECAY→DISTURBED re-excitation: `accel_err_var > max(baseline × K_HIGH, K_ABS_MIN_ACCEL_VAR)`, discard decay data
- [x] 3.6 Implement FREE_DECAY timeout: check elapsed time against `CONFIG_MONITOR_FREE_DECAY_TIMEOUT_S`, publish partial on expiry

## 4. Buffer Management

- [x] 4.1 IDLE→DISTURBED: reset disturbance buffer, copy short buffer as pre-roll (refactor existing logic)
- [x] 4.2 DISTURBED→FREE_DECAY: record `decay_start_index_` at current write position; do not reset buffer (sway spans both states)
- [x] 4.3 FREE_DECAY→IDLE: pass `[decay_start_index_, write_index_)` range to FFT/damping computations
- [x] 4.4 FREE_DECAY→DISTURBED (re-excitation): discard decay tracking, reset `decay_start_index_`, continue sway accumulation
- [x] 4.5 DISTURBED buffer refresh (N_DPAD): publish sway stats only (no FFT/damping), reset buffer, copy short buffer

## 5. Per-Axis FFT

- [x] 5.1 Refactor `ComputeNaturalFrequency()` to accept a single-axis data array instead of computing magnitude internally
- [x] 5.2 Create wrapper that calls per-axis FFT twice (roll buffer, pitch buffer) and writes `natural_freq_roll_hz` and `natural_freq_pitch_hz` to result
- [x] 5.3 Apply de-mean, Hann window, and Welch averaging per axis (reuse existing windowing logic)
- [x] 5.4 Remove magnitude computation `√(roll²+pitch²)` from FFT path
- [x] 5.5 Update `psd_accum_` to handle two axes (either two arrays or sequential reuse)

## 6. State-Specific Publication

- [x] 6.1 IDLE publication: baseline tilt stats (mean, variance), zero out freq/damping/sway fields, set `state = IDLE`
- [x] 6.2 DISTURBED publication (on refresh or transition to FREE_DECAY): sway pp_max/pp_mean, zero out freq/damping, set `state = DISTURBED`
- [x] 6.3 FREE_DECAY publication (on exit to IDLE or timeout): per-axis freq + damping + final sway stats, set `state = FREE_DECAY`
- [x] 6.4 Refactor `ComputeAndPublish()` into state-specific publish methods

## 7. Logger Adaptation

- [x] 7.1 Update MQTT JSON serialization to include `natural_freq_roll_hz`, `natural_freq_pitch_hz`, and `state` fields
- [x] 7.2 Handle FREE_DECAY event publications in logger event handler
- [x] 7.3 Update `mqtt_interface.md` documentation with new payload schema

## 8. Dashboard Adaptation

- [x] 8.1 Add FREE_DECAY state indicator to dashboard display
- [x] 8.2 Display per-axis natural frequency values (`f_n_roll`, `f_n_pitch`) instead of single `f_n`
- [x] 8.3 Update dashboard state rendering to handle 3 states

## 9. Testing & Verification

- [x] 9.1 Verify accel_err_var baseline accumulation during IDLE with device at rest
- [x] 9.2 Test IDLE→DISTURBED transition by manually shaking device; confirm accel_err triggers before tilt
- [x] 9.3 Test DISTURBED→FREE_DECAY transition: shake device, stop, verify debounce timing and state change
- [x] 9.4 Test per-axis FFT: apply known-frequency oscillation in one axis, verify correct frequency reported
- [x] 9.5 Test FREE_DECAY→IDLE: verify freq and damping published at transition
- [x] 9.6 Test re-excitation: shake during FREE_DECAY, verify return to DISTURBED
- [x] 9.7 Test timeout: hold device in slow sway exceeding timeout, verify partial results published
