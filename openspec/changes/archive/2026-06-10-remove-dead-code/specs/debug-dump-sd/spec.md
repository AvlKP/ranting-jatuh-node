# debug-dump-sd Delta Spec

## REMOVED Requirements

### Requirement: Debug Dump Kconfig Toggle

**Reason**: `CONFIG_MONITOR_DEBUG_DUMP` gates the centerline modal debug dump path. That path depends on `AnalyzeModalAxis`, `ModalAxisResult`, `PeakList`, and centerline pair data that are removed by this change. Keeping the Kconfig toggle would expose a build option for deleted behavior.

**Migration**: No migration in this change. A future debug feature should define a new active-pipeline dump format around `AnalyzeImuEvent`, `FindDecayOnsetTkeo`, `ComputeSignedAxisNaturalFrequency`, and `ComputePeakHoldDamping`.

### Requirement: Debug Dump Trigger

**Reason**: The current trigger calls `DumpDebugToSD()` after the active `AnalyzeImuEvent()` path, but passes `roll_modal_scratch_` and `pitch_modal_scratch_` that are never written by active code. The dumped modal details are stale/zeroed and no longer represent production analysis.

**Migration**: No migration in this change. Production `MonitorResult` publication remains unchanged.

### Requirement: Debug Dump File Format

**Reason**: The format is centered on deprecated per-axis centerline modal analysis tags (`DECAY`, `PEAKS`, `COLLAPSED`, `PAIRS`, `MODAL_TIME_US`). These tags do not describe the active TKEO decay onset + signed-axis FFT + peak-hold envelope pipeline.

**Migration**: Existing debug CSV files remain historical artifacts. New debug output requires a separate spec.

### Requirement: Debug Analysis Python Script

**Reason**: The script contract is tied to parsing and visualizing removed centerline modal debug dump fields.

**Migration**: Remove centerline-specific analyzer code with the firmware dump path. Reintroduce analyzer support in a future active-pipeline debug change if needed.

### Requirement: Debug Dump Centerline Data

**Reason**: Centerline pair and collapsed extrema data are produced only by deprecated `AnalyzeModalAxis` methods removed by this change.

**Migration**: No migration needed.
