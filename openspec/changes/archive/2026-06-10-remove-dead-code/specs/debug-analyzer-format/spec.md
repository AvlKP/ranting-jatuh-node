# debug-analyzer-format Delta Spec

## REMOVED Requirements

### Requirement: Parse New Dump Tags

**Reason**: The `MODAL_TIME_US`, `COLLAPSED`, and `PAIRS` tags are emitted only by the deprecated centerline modal debug dump format removed in this change.

**Migration**: Historical dump files may still be inspected with older tooling. Current firmware no longer emits these tags.

### Requirement: JSON Output Includes Centerline Data

**Reason**: Firmware will no longer emit centerline pairs, collapsed extrema counts, or modal-analysis timing from `AnalyzeModalAxis`.

**Migration**: No migration in this change.

### Requirement: Python Centerline-Corrected Frequency Recomposition

**Reason**: This recomputation mirrors deprecated firmware centerline residual analysis. Active firmware estimates natural frequency with `ComputeSignedAxisNaturalFrequency` on the dominant signed gyro axis.

**Migration**: Future analyzer work should target active-pipeline fields if a new debug dump format is added.

### Requirement: Python Centerline-Corrected Damping Regression

**Reason**: This recomputation mirrors deprecated `ComputeDampingRegression` over pair-envelope amplitudes. Active firmware uses `ComputePeakHoldDamping` over calibrated gyro magnitude.

**Migration**: Future analyzer work should recompute peak-hold envelope damping if active-pipeline debug data is added.

### Requirement: Plot Comparison Includes Centerline Stats

**Reason**: Centerline statistics disappear with the centerline modal debug format.

**Migration**: Remove centerline rows from current analyzer output, or leave them only for explicitly historical dump parsing outside firmware support.
