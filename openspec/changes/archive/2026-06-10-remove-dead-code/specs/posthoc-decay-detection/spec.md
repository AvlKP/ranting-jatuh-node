# posthoc-decay-detection Delta Spec

## REMOVED Requirements

### Requirement: Decay Region Identification

**Reason**: The old decay region identification method (maximum absolute amplitude peak in tilt buffer) has been replaced by TKEO energy-burst decay onset detection (`FindDecayOnsetTkeo`). The new method identifies free-decay start from the last TKEO energy burst, snaps to nearest local gmag peak, and validates ringdown quality. Specified in `free-decay-analysis`.

**Migration**: No migration needed. The `AnalyzeImuEvent` function in `monitor.cpp` uses the new TKEO-based decay onset exclusively.

### Requirement: Peak Envelope Tracking

**Reason**: The lobe-collapsed peak/trough pair amplitude tracking method (centerline subtraction + residual envelope) has been replaced by a peak-hold envelope on calibrated gyro magnitude (`ComputePeakHoldDamping`). The old method's types (`ExtremaList`, `CenterlinePairList`) and methods (`DetectRawExtrema`, `CollapseExtremaLobes`, `BuildCenterlinePairs`, `SelectPairEnvelope`, `SubtractCenterline`, `ComputeResidualNaturalFrequency`) have zero production call sites.

**Migration**: No migration needed. `ComputePeakHoldDamping` uses the asymmetric peak-hold envelope specified in `envelope-damping-regression` and `free-decay-analysis`.

### Requirement: Configurable Peak Detection Parameters

**Reason**: The peak detection parameters (`PEAK_MIN_SPACING`, `PEAK_MIN_AMPLITUDE`, `CENTERLINE_MIN_AMPLITUDE`, `CENTERLINE_LOBE_REVERSAL`) exist in Kconfig but are only consumed by the now-obsolete modal analysis path (centerline pairs, lobe collapse). The active sway computation in `ComputeSwayAndDamping` also reads `peak_min_amplitude_deg` and `peak_min_spacing` from the config for roll/pitch extrema detection. These config fields must be preserved for sway statistics.

**Migration**: Keep `CONFIG_MONITOR_PEAK_MIN_AMPLITUDE_X10` and `CONFIG_MONITOR_PEAK_MIN_SPACING_SAMPLES` in Kconfig — they are still consumed by `ComputeSwayAndDamping`. Remove only `CENTERLINE_MIN_AMPLITUDE` and `CENTERLINE_LOBE_REVERSAL` Kconfig keys and their corresponding `MonitorConfig` fields — these are exclusive to the dead centerline pair construction.

### Requirement: Lobe-Collapsed Firmware Extrema Selection

**Reason**: The lobe collapse algorithm (`CollapseExtremaLobes`) operates on the dead modal analysis path. No production code calls it. The active path (`AnalyzeImuEvent`) does not use extrema lobe collapse.

**Migration**: No migration needed. Active damping uses peak-hold envelope on raw gmag, not extrema-based envelope.
