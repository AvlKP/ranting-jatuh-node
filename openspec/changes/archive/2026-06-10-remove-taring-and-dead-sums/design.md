## Context

The monitor has two vestigial subsystems:

1. **Taring**: At startup, the complementary filter settles for `TARE_SETTLE_SAMPLES`, then averages `TARE_SAMPLES` of roll/pitch to compute a baseline offset. All subsequent samples have this offset subtracted. The detection pipeline uses gyro magnitude and TKEO -- not tilt. The analysis pipeline produces offset-invariant results (variance, sway pp, damping, natural frequency). Taring only shifts `roll_mean` and `pitch_mean` in published results from absolute branch angle to ~0.

2. **Short buffer running sums**: `roll_short_sum_`, `roll_short_sq_sum_`, `pitch_short_sum_`, `pitch_short_sq_sum_` are maintained on every sample (subtract old, add new) but never consumed for computation. They were part of Gen 1 variance-based IDLE/DISTURBED detection, already removed. The short buffer arrays themselves remain active for IDLE→DISTURBED history pre-fill.

After the prior dead-code cleanup, the system has a clear pipeline: DSP detector (gmag+TKEO) for state transitions, and `AnalyzeImuEvent` (TKEO decay onset + signed-axis FFT + peak-hold envelope) for post-hoc analysis. Neither pipeline touches tilt-based means or short buffer variance sums.

## Goals / Non-Goals

**Goals:**
- Remove taring infrastructure: member variables, `Update()` block, `Init()` initialization, Kconfig keys, `sdkconfig.defaults` entries
- Remove dead short buffer running sums: member variables and their maintenance in `PushSample()`
- Remove `CONFIG_MONITOR_TARE_ENABLE` and its compile-time guard -- the non-taring path becomes the only path

**Non-Goals:**
- Removing the short buffer ARRAYS (`roll_short_`, `pitch_short_`, etc.) -- still used for IDLE→DISTURBED history pre-fill
- Removing the complementary filter -- provides smooth orientation for dashboard
- Adding server-side baseline subtraction -- out of scope for firmware cleanup
- Changing any published field structure or protocol

## Decisions

### Decision 1: Remove taring entirely, not just disable it

The taring Kconfig defaults to `y` (enabled). Simply flipping the default would still leave the code. Full removal eliminates the Kconfig toggle, the compile-time guard, all member variables, and the startup delay.

**Alternatives considered**: Keep the Kconfig toggle but default to `n`. Rejected -- adds maintenance burden for a feature that provides no analytical value (offset-invariant results).

### Decision 2: Keep short buffer arrays, remove only the running sums

The short buffer stores pre-disturbance samples used to backfill the history buffer on IDLE→DISTURBED transition. This preserves the ability to analyze pre-onset data. Only the running sum accumulators (`roll_short_sum_`, `roll_short_sq_sum_`, `pitch_short_sum_`, `pitch_short_sq_sum_`) are dead.

**Alternatives considered**: Remove the entire short buffer. Rejected -- the backfill of pre-disturbance data into the history buffer provides context for onset detection.

### Decision 3: No migration for downstream consumers

Published `roll_mean` and `pitch_mean` change semantics from "deviation from resting" to "absolute orientation." This is documented in the proposal as a behavioral change but requires no protocol or structure changes to `MonitorResult`.

**Alternatives considered**: Add a `tilt_offset_deg` field to `MonitorResult` so the server can reconstruct tared values. Rejected -- the server can subtract the first few IDLE means as a baseline. Adding fields expands scope.

## Risks / Trade-offs

| Risk | Mitigation |
|------|-----------|
| Downstream dashboard/server assumes ~0 mean for resting branch | Document the semantic change. Server can subtract first IDLE mean as virtual baseline. |
| Removing taring during refactoring accidentally breaks the non-tare path | The non-tare path is already compiled and exercised when `CONFIG_MONITOR_TARE_ENABLE=n`. Removal simplifies to this single path. |
| Short buffer running sums removal misses a read site | Grep audit before removal. The exploration confirmed zero consumption of these sums. |
| `sdkconfig.defaults` references removed Kconfig keys | Remove `CONFIG_MONITOR_TARE_ENABLE=y` and `CONFIG_MONITOR_TARE_SAMPLES=100` from defaults. `CONFIG_MONITOR_TARE_SETTLE_SAMPLES` already absent from defaults. |
