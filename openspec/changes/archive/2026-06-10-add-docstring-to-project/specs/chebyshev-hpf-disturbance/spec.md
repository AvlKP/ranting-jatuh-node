# chebyshev-hpf-disturbance Specification Delta

## REMOVED Requirements

### Requirement: Chebyshev HPF Per-Axis Filtering
**Reason**: The ChebyshevType 1 HPF-based disturbance detection path is superseded by the TKEO-based `DspDisturbanceDetector`. The HPF class is unused in production code and only referenced in test files. The architecture document has been updated to reflect the current DSP-based detection method.

**Migration**: No migration needed. The TKEO-based detection provides equivalent disturbance gating with better transient response. Configuration keys `CONFIG_MONITOR_DSP_TKEO_*` and `CONFIG_MONITOR_DSP_GMAG_*` are the active replacements.

### Requirement: HPF Settle Period at Startup
**Reason**: Removed with the HPF-based detection. The current TKEO-based detector has no filter transient and does not require a settle period.

**Migration**: The existing tare settle mechanism (`CONFIG_MONITOR_TARE_SETTLE_SAMPLES`) handles orientation offset removal, which is separate from disturbance detection.

### Requirement: HPF Threshold Detection
**Reason**: The HPF magnitude threshold detection is replaced by the Schmitt trigger with TKEO energy + gmag thresholds in `DspDisturbanceDetector::Update`.

**Migration**: Use `CONFIG_MONITOR_DSP_TKEO_HIGH_X10` for entry and `CONFIG_MONITOR_DSP_TKEO_LOW_X10` for exit thresholds. The `DISTURBED_EXIT_DEBOUNCE` config key is retained and reused by the DSP detector.

### Requirement: HPF Configuration via Kconfig
**Reason**: Kconfig keys `CONFIG_MONITOR_HPF_THRESHOLD_X1000` and `CONFIG_MONITOR_HPF_SETTLE_SAMPLES` were never created and do not exist in any Kconfig file. The spec documented intended configuration that was never implemented before the HPF approach was abandoned.

**Migration**: No migration needed. These keys never existed in production Kconfig.
