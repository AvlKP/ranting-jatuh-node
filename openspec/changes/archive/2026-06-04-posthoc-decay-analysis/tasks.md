## 1. FSM Simplification

- [x] 1.1 Remove `FREE_DECAY` from `NodeState` enum in `monitor.hpp`. Update to two states: `IDLE`, `DISTURBED`.
- [x] 1.2 Add `CONFIG_MONITOR_DISTURBED_EXIT_DEBOUNCE` to `Kconfig` (int, default 64, range 1–1000). Add `disturbed_exit_debounce_counter_` member to `Monitor` class.
- [x] 1.3 Remove `CONFIG_MONITOR_K_MID_X100`, `CONFIG_MONITOR_FREE_DECAY_DEBOUNCE`, `CONFIG_MONITOR_FREE_DECAY_TIMEOUT_S` from `Kconfig`.
- [x] 1.4 Rewrite DISTURBED state transition logic in `PushSample()`: replace DISTURBED→FREE_DECAY with DISTURBED→IDLE using `K_LOW` threshold + exit debounce counter (reset on any sample above `K_LOW`).
- [x] 1.5 Remove entire FREE_DECAY state block from `PushSample()` (re-excitation, decay complete, timeout logic). Remove `free_decay_entry_time_us_`, `free_decay_debounce_counter_`, `decay_start_index_`, `decay_sample_count_` members that are superseded by post-hoc logic.
- [x] 1.6 Update `ComputeAndPublish()` call at DISTURBED→IDLE transition to trigger post-hoc analysis (pass entire buffer for retroactive decay detection + FFT + damping).

## 2. Peak Detection Tuning

- [x] 2.1 Change `CONFIG_MONITOR_PEAK_MIN_SPACING_SAMPLES` default from 5 to 2 in `Kconfig`.
- [x] 2.2 Change `CONFIG_MONITOR_PEAK_MIN_AMPLITUDE` to scaled integer `CONFIG_MONITOR_PEAK_MIN_AMPLITUDE_X10` in `Kconfig` (default 5, representing 0.5°, range 1–900). Update all references in `monitor.cpp` and `monitor.hpp` to divide by 10.0f.

## 3. Post-Hoc Decay Region Detection

- [x] 3.1 Implement `FindDecayRegion()` method: scan the stored DISTURBED buffer to find all peaks/troughs meeting `PEAK_MIN_AMPLITUDE` and `PEAK_MIN_SPACING` criteria. Return the index and amplitude of each extremum.
- [x] 3.2 In `FindDecayRegion()`, locate the extremum with maximum absolute amplitude as the decay start candidate.
- [x] 3.3 From the max peak, walk forward collecting peaks whose absolute amplitude is monotonically declining. Stop on amplitude increase or buffer end. Output: `decay_start_index`, `decay_sample_count`, and the collected peak list (amplitude + sample index).
- [x] 3.4 Handle edge cases: no peaks found → decay region empty, freq/damping = 0.0. Max peak near buffer end (< 4 peaks after) → damping = 0.0, FFT still attempted on available data.

## 4. Envelope Damping Regression

- [x] 4.1 Implement `ComputeDampingRegression()` method: takes peak list (amplitude, time) from `FindDecayRegion()` and natural frequency from FFT. Performs OLS linear regression on `ln(|amplitude|)` vs `time_seconds`.
- [x] 4.2 Compute slope from regression. Derive damping ratio: `ζ = |slope| / ωₙ` where `ωₙ = 2π × natural_freq_hz`. Guard: if `natural_freq_hz == 0` or slope > 0 (increasing amplitude), output ζ = 0.0.
- [x] 4.3 Enforce minimum peak count: require ≥ 4 peaks for regression. Below threshold → ζ = 0.0.
- [x] 4.4 Compute per-axis independently: call regression separately for roll and pitch peak lists with their respective FFT frequencies.

## 5. Integration

- [x] 5.1 Update `ComputeAndPublish()`: on DISTURBED→IDLE, call `FindDecayRegion()` to get decay indices + peak list, then `ComputeNaturalFrequency()` on the decay segment, then `ComputeDampingRegression()` with FFT results. Sway stats computed over entire DISTURBED buffer as before.
- [x] 5.2 Remove old `ComputeSwayAndDamping()` decay-portion damping logic (log decrement with `kDecaySpan=3`). Keep sway peak-to-peak computation (operates on full buffer, unchanged).
- [x] 5.3 Update `MonitorResult` publication: results from DISTURBED→IDLE exit carry `state = DISTURBED` with populated freq/damping fields. Remove zeroing of freq/damping for DISTURBED publications (now they carry real values on exit).

## 6. Logger and Dashboard Updates

- [x] 6.1 Remove `FREE_DECAY` string mapping from logger CSV state field. Verify DISTURBED rows now carry freq/damping data.
- [x] 6.2 Remove `FREE_DECAY` state display from dashboard. Update state indicator to two-state (IDLE/DISTURBED).
- [x] 6.3 Update any dashboard parameter display that was conditional on FREE_DECAY state to show freq/damping from DISTURBED exit results.

## 7. Verification

- [x] 7.1 Build the firmware: `idf.py build`
- [x] 7.2 (If possible in the current environment) Run unit tests or simulation to verify DISTURBED→IDLE transition.
- [x] 7.3 Bench test with 20mm branch sample as cantilever: confirm IDLE→DISTURBED→IDLE transitions with post-hoc freq/damping in logs.
- [ ] 7.4 Verify detected frequency is in expected range (3–12Hz for bare branch, lower with tip mass). Verify damping ratio is in expected range (0.01–0.05).
- [ ] 7.5 Test edge cases: very small disturbance (below peak threshold), prolonged disturbance (buffer refresh), rapid re-excitation during exit debounce.
