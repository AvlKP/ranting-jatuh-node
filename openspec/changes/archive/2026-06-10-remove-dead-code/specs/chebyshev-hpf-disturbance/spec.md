# chebyshev-hpf-disturbance Delta Spec

## REMOVED Requirements

### Requirement: Chebyshev HPF Per-Axis Filtering

**Reason**: The Chebyshev Type 1 HPF disturbance detection strategy was designed but never integrated into the production monitor. The disturbance detection state machine uses TKEO (Teager-Kaiser Energy Operator) on calibrated gyro magnitude via `DspDisturbanceDetector`, as specified in `imu-event-analysis`. The ChebyshevHpf class exists in `chebyshev_hpf.hpp` but has zero production call sites.

**Migration**: No migration needed. The `DspDisturbanceDetector` in `monitor.cpp` replaced the HPF approach before the HPF was ever wired into the main data path.

### Requirement: HPF Settle Period at Startup

**Reason**: Same as above — settle-period logic was designed for the HPF but never integrated. The tare/settle mechanism in the active code handles adaptive complementary filter convergence, not HPF transients.

**Migration**: No migration needed. Existing `tare_settle_accumulated_` handles complementary filter startup.

### Requirement: HPF Threshold Detection

**Reason**: HPF magnitude threshold detection never drove state transitions in production. The Schmitt trigger in `DspDisturbanceDetector` uses TKEO and gmag thresholds (configurable via `CONFIG_MONITOR_DSP_TKEO_HIGH_X10`, `CONFIG_MONITOR_DSP_TKEO_LOW_X10`, `CONFIG_MONITOR_DSP_GMAG_ONSET_X100`, `CONFIG_MONITOR_DSP_GMAG_QUIET_X100`).

**Migration**: No migration needed. TKEO-based thresholds already active in production.

### Requirement: HPF Configuration via Kconfig

**Reason**: The Kconfig keys `CONFIG_MONITOR_HPF_THRESHOLD_X1000` and `CONFIG_MONITOR_HPF_SETTLE_SAMPLES` were specified but never created in any Kconfig file. The DSP detector's Kconfig keys (`CONFIG_MONITOR_DSP_*`) are the active equivalents.

**Migration**: No migration needed. DSP detector Kconfig keys already exist in `components/monitor/Kconfig`.
