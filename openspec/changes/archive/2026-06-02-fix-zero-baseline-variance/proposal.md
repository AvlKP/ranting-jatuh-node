## Why

When the device sits still in IDLE for an extended period, the 5-minute window variance converges to near-zero (sensor noise floor only). The current state transition thresholds are purely relative (`short_var > baseline * K_IDLE`), so multiplying near-zero by any factor still yields near-zero. This causes any tiny movement — even sensor noise — to trigger IDLE→DISTURBED, and returning to IDLE becomes nearly impossible since the DISTURBED→IDLE threshold (`short_var < baseline * K_DISTURBED`) demands near-perfect stillness.

## What Changes

- Add an **absolute minimum variance threshold** (`K_ABS_MIN_VAR`) as a Kconfig parameter, representing the sensor noise floor below which variance is meaningless.
- Modify the IDLE→DISTURBED transition to use `max(baseline * K_IDLE, K_ABS_MIN_VAR)` as the effective threshold instead of `baseline * K_IDLE` alone.
- Modify the DISTURBED→IDLE transition to use `max(baseline * K_DISTURBED, K_ABS_MIN_VAR)` as the effective threshold instead of `baseline * K_DISTURBED` alone.
- Clamp the stored baseline variance (`idle_5min_roll_var_`, `idle_5min_pitch_var_`) to never fall below `K_ABS_MIN_VAR`, so downstream consumers also see a sane floor.

## Capabilities

### New Capabilities

_None_

### Modified Capabilities

- `node-state-machine`: State transition thresholds now incorporate an absolute minimum variance floor alongside the existing relative multipliers. Prevents false transitions when baseline converges to near-zero.

## Impact

- **Code**: `monitor.cpp` — `PushSample()` state transition logic (lines ~315-382), baseline update in `Update()` (lines ~240-242). `monitor.hpp` — new member for the config value.
- **Config**: `components/monitor/Kconfig` — one new integer parameter (`MONITOR_ABS_MIN_VAR_X1000` or similar scaled integer).
- **Behavior**: Devices left still will no longer false-trigger into DISTURBED. No impact on devices experiencing real disturbances (where baseline variance is well above the floor).
- **Breaking**: None. Existing deployments gain a sensible default floor; behavior unchanged when baseline is above the floor.
