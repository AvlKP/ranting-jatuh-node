# notebook-centerline-modal-analysis Delta Spec

## REMOVED Requirements

### Requirement: Centerline Modal Analysis

**Reason**: The notebook-based centerline modal analysis (notebook workflow for baseline-robust modal analysis of pull-and-release IMU logs) is a Python reference that was superseded by the TKEO-based pipeline. The C++ firmware equivalent (`AnalyzeModalAxis`) has zero production call sites and is gated behind `CONFIG_MONITOR_DEBUG_DUMP`. The active post-hoc analysis uses `AnalyzeImuEvent` (TKEO decay onset + per-axis FFT + peak-hold envelope damping) as specified in `free-decay-analysis`.

**Migration**: No migration needed. The Python reference in `imu_algorithms/` and the active `AnalyzeImuEvent` C++ path use the TKEO-based pipeline exclusively. This notebook workflow remains as a historical artifact in the codebase but the spec is deprecated.


