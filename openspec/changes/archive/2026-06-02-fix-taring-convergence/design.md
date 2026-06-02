# Fix Taring Convergence - Design

## Architecture Updates
No major architecture changes. The taring logic remains in `monitor::Monitor::Update`. We will simply add a settling state before the taring state.

## Implementation Details

### Configuration
Update `components/monitor/Kconfig`:
- Add `CONFIG_MONITOR_TARE_SETTLE_SAMPLES` (int). Default to `500` (which is 5 seconds at 100Hz).
- Place it under the Taring section.

### Monitor Class State
Update `components/monitor/include/monitor.hpp`:
- Add a counter `std::size_t tare_settle_accumulated_{0U};` to track how many settling samples have passed.

### Monitor Update Logic
Update `components/monitor/monitor.cpp`:
- Inside `#if CONFIG_MONITOR_TARE_ENABLE`, before accumulating taring samples, check if `tare_settle_accumulated_ < CONFIG_MONITOR_TARE_SETTLE_SAMPLES`.
- If so, increment `tare_settle_accumulated_`, but DO NOT accumulate `roll_tare_sum_` and DO NOT increment `tare_samples_accumulated_`.
- During the settling period, `current_roll` and `current_pitch` will remain unmodified (since we don't have an offset yet), which means `roll_history_` and `stream_samples_` will temporarily store the untared (but converging) values.
- Once settling is complete, `tare_samples_accumulated_` will start increasing until `CONFIG_MONITOR_TARE_SAMPLES`.
- Once taring completes, the retroactive loop will subtract the offset from `roll_history_` and `stream_samples_`. This correctly handles both the settling and taring period history.

## Alternative Approaches
- **Initialize filter from accel:** We could initialize the filter's state (quaternion/angles) directly from the first accelerometer reading using geometry. This would eliminate settling time entirely. However, it requires modifying the `ComplementaryFilter` interface to support initialization, which is more invasive than a simple delay. A delay is a robust and safer change.

## Testing Strategy
1. Build the firmware to ensure no compilation errors.
2. Flash and monitor the serial logs. Ensure the log line `"Taring complete..."` appears after `(Settle Samples + Tare Samples) / Rate` seconds (e.g., 6 seconds at 100Hz instead of 1 second).
3. Observe the dashboard. The `Roll` and `Pitch` charts should initially show the converging values, then snap to exactly `0.0` at steady state once taring completes and retroactively fixes the history buffer.
