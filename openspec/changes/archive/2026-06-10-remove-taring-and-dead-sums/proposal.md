## Why

Taring subtracts a startup baseline so tilt mean hovers near zero. But the detection pipeline (gmag + TKEO) and analysis pipeline results (variance, sway pp, damping, natural frequency) are all offset-invariant. Taring only shifts `roll_mean` and `pitch_mean` from "useful absolute branch angle" to "cosmetic zero" while costing ~40 lines of code, 7 member variables, and ~11.5s startup delay. The short buffer rolling sums (`roll_short_sum_` etc.) are maintained on every sample but never consumed for computation -- vestigial from Gen 1 variance-based detection, removed in the prior dead-code cleanup.

## What Changes

- **Remove taring infrastructure**: `taring_complete_`, `roll_offset_`, `pitch_offset_`, tare sum accumulators, settle counter, the `Update()` taring block, and `Init()` tare initialization
- **Remove dead short buffer sums**: `roll_short_sum_`, `roll_short_sq_sum_`, `pitch_short_sum_`, `pitch_short_sq_sum_` and their maintenance code in `PushSample()`
- **Remove taring Kconfig keys**: `CONFIG_MONITOR_TARE_ENABLE`, `CONFIG_MONITOR_TARE_SAMPLES`, `CONFIG_MONITOR_TARE_SETTLE_SAMPLES`
- **Keep short buffer arrays**: `roll_short_`, `pitch_short_`, `gmag_short_`, etc. remain for IDLE→DISTURBED pre-fill

## Capabilities

### New Capabilities

None.

### Modified Capabilities

None. No existing specs reference taring or the short buffer sums. This is implementation cleanup only.

## Impact

- **Affected files**: `components/monitor/include/monitor.hpp`, `components/monitor/monitor.cpp`, `components/monitor/Kconfig`, `sdkconfig.defaults`, `notebook/NOTES.md`
- **Breaking changes**: None. Published `MonitorResult` fields (`roll_mean`, `pitch_mean`) will now carry absolute branch angle instead of zero-centered values. Downstream consumers that assumed ~0 mean must subtract a baseline. Variance, sway, damping, and natural frequency values remain identical.
- **Risk**: Low. No changes to the detection or analysis pipelines.
