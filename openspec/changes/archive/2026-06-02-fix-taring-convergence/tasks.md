## 1. Add Configuration

- [x] 1.1 In `components/monitor/Kconfig`, add `config MONITOR_TARE_SETTLE_SAMPLES` with type `int`, default `500`, and appropriate prompt/help description below `CONFIG_MONITOR_TARE_ENABLE`.

## 2. Implement Settling Delay

- [x] 2.1 In `components/monitor/include/monitor.hpp`, add `std::size_t tare_settle_accumulated_{0U};` to the `Monitor` class.
- [x] 2.2 In `components/monitor/monitor.cpp::Monitor::Init`, reset `tare_settle_accumulated_ = 0U;` alongside the other taring variables.
- [x] 2.3 In `components/monitor/monitor.cpp::Monitor::Update`, modify the `else` block of `if (taring_complete_)`. Add a check: `if (tare_settle_accumulated_ < static_cast<std::size_t>(CONFIG_MONITOR_TARE_SETTLE_SAMPLES))` and increment `tare_settle_accumulated_` instead of accumulating `roll_tare_sum_`.
- [x] 2.4 Only accumulate `roll_tare_sum_` and increment `tare_samples_accumulated_` if the settling period has completed.
- [x] 2.5 Ensure the retroactive offset subtraction (`roll_history_[i] -= roll_offset_`) still uses `write_index_` and `stream_count_` which will now cover both the settling and taring samples, retroactively correcting the entire history buffer.

## 3. Verify

- [x] 3.1 Build firmware and ensure no compilation errors.
- [ ] 3.2 Flash and monitor the logs to verify taring completion logs appear after ~6 seconds instead of ~1 second.
- [ ] 3.3 Check the dashboard to confirm steady state is 0.0 for roll and pitch.
