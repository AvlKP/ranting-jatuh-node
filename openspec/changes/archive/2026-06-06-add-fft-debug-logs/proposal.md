## Why

The natural frequency (FFT) and damping ratio (peak envelope regression) calculations produce end-result values logged in the parameter CSV, but the intermediate data — PSD spectrum, detected peaks, and decay region boundaries — is invisible. When results are suspect (e.g., freq=0 Hz, zeta=0.0, or implausible values), there is no way to trace back to the root cause. Adding FFT and peak envelope debug logs closes this observability gap.

## What Changes

- New `debug_fft.csv` file: per-bin PSD rows (`timestamp_ms,axis,bin,freq_hz,psd_power`) written at each DISTURBED→IDLE analysis event
- New `debug_peaks.csv` file: per-peak rows (`timestamp_ms,axis,peak_idx,time_s,amplitude_deg,log_amplitude`) written at each DISTURBED→IDLE analysis event
- Monitor captures `local_psd[]` and peak lists into debug-dedicated members during `ComputeAxisNaturalFrequency()` and `FindDecayRegion()`
- Logger formats and flushes the new CSV files using the existing batched-write pattern (ring buffer + periodic flush + overwrite-on-boot)
- Gated behind existing `CONFIG_APP_DEBUG_CSV_LOGS` Kconfig option — no new configuration needed

## Capabilities

### New Capabilities
- `fft-debug-logs`: CSV debug logging of FFT power spectral density and peak envelope data for offline analysis of natural frequency and damping ratio calculations

### Modified Capabilities
- `debug-csv-logs`: Extended to cover the two new CSV file types (fft, peaks) in addition to the existing stream-sample debug.csv. Requirement: the system SHALL gate FFT/peaks debug logging behind the same `CONFIG_APP_DEBUG_CSV_LOGS` flag and follow the same overwrite-on-boot + batched-flush pattern.

## Impact

- Affected code: `monitor.hpp` (new debug members), `monitor.cpp` (capture PSD and peaks), `logger.cpp` (format + flush new CSVs), `logger_storage.cpp` (file paths + append/flush functions), `logger_internal.hpp` (if new buffer arrays needed)
- No API changes, no dependency changes, no breaking changes
- SD card write volume: ~6 KB per analysis event (rare, at most every few minutes)
