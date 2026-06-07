## 1. Configuration and Data Structures

- [x] 1.1 Add monitor Kconfig entries for centerline minimum amplitude, lobe reversal threshold, modal FFT minimum frequency, and modal FFT maximum frequency
- [x] 1.2 Extend `MonitorConfig` with modal thresholds derived from scaled Kconfig values
- [x] 1.3 Add fixed-size private structs for raw extrema, collapsed extrema, centerline pairs, and modal axis results
- [x] 1.4 Add one reusable fixed-size residual scratch buffer and any bounded pair/extrema buffers needed by analysis

## 2. Centerline Modal Helpers

- [x] 2.1 Implement raw local extrema detection over a logical decay window using existing physical-index mapping
- [x] 2.2 Implement lobe-collapsed extrema selection with same-kind strongest replacement and opposite-kind reversal threshold
- [x] 2.3 Implement adjacent opposite-extrema centerline pair construction with centerline minimum amplitude filtering
- [x] 2.4 Implement piecewise-linear centerline subtraction into residual scratch, including endpoint hold behavior
- [x] 2.5 Implement pair-envelope selection from the largest half peak-to-peak amplitude forward while amplitudes are non-increasing

## 3. FFT and Damping Integration

- [x] 3.1 Add bounded FFT bin selection for both zero-padded and Welch paths
- [x] 3.2 Update axis natural-frequency computation to accept residual scratch data and modal frequency bounds
- [x] 3.3 Reuse damping regression on pair-envelope amplitudes and times
- [x] 3.4 Wire DISTURBED->IDLE modal analysis so MonitorResult frequency and damping fields come from centerline-compensated outputs
- [x] 3.5 Keep intermediate DISTURBED buffer-refresh publications reporting sway only with zero frequency and damping

## 4. Diagnostics

- [x] 4.1 Extend `CONFIG_MONITOR_DEBUG_DUMP` output with collapsed extrema counts and centerline pair amplitudes/times
- [x] 4.2 Keep raw tilt dump available for notebook comparison
- [x] 4.3 Add debug timing around modal analysis using `esp_timer_get_time()` when debug dump is enabled

## 5. Tests

- [x] 5.1 Add Unity tests for raw extrema detection with spacing filter
- [x] 5.2 Add Unity tests for lobe collapse with multiple local maxima/minima inside one lobe
- [x] 5.3 Add Unity tests for centerline pair construction and residual endpoint behavior
- [x] 5.4 Add Unity tests for bounded FFT bin selection, including empty-band and clamped-band cases
- [x] 5.5 Add Unity tests for pair-envelope damping with sufficient and insufficient amplitudes
- [x] 5.6 Add a synthetic drifting-baseline decay test that verifies centerline modal analysis does not require oscillation around zero

## 6. Verification

- [x] 6.1 Compare firmware helper output against notebook-derived expected values for `logs/raw_log_7.csv` or an exported compact fixture
- [x] 6.2 Source `C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1` and run ESP-IDF build for ESP32-S3
- [x] 6.3 Inspect build output or map file for RAM/flash growth from modal scratch buffers
- [x] 6.4 Run relevant Unity tests
- [x] 6.5 Confirm `openspec status --change port-centerline-modal-analysis-to-firmware` reports all required artifacts complete
