## 1. Configuration

- [x] 1.1 Add `MONITOR_ABS_MIN_VAR_X10000` to `components/monitor/Kconfig` with default 50, range 1-10000, under the existing state machine threshold params
- [x] 1.2 Verify `idf.py menuconfig` shows the new parameter and sdkconfig generates correctly

## 2. Core Implementation

- [x] 2.1 In `PushSample()` IDLE→DISTURBED transition (monitor.cpp ~L324-326), replace `idle_5min_roll_var_ * k_idle` with `std::max(idle_5min_roll_var_ * k_idle, abs_min_var)` for both roll and pitch
- [x] 2.2 In `PushSample()` DISTURBED→IDLE transition (monitor.cpp ~L356-357), replace `idle_5min_pitch_var_ * k_disturbed` with `std::max(idle_5min_pitch_var_ * k_disturbed, abs_min_var)` for both roll and pitch
- [x] 2.3 Compute `abs_min_var` as `static_cast<float>(CONFIG_MONITOR_ABS_MIN_VAR_X10000) / 10000.0f` — either as a local const in `PushSample()` or a member initialized once

## 3. Verification

- [x] 3.1 Build firmware with `idf.py build` — confirm no compile errors
- [x] 3.2 Flash device, leave still for >5 minutes, confirm IDLE baseline log shows near-zero variance but no false DISTURBED transition
- [x] 3.3 While in IDLE with near-zero baseline, apply gentle tap — confirm transition fires only for real disturbance above the floor
- [x] 3.4 Confirm DISTURBED→IDLE transition succeeds (device returns to IDLE after disturbance subsides)
