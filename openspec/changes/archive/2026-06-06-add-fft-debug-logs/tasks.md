## 1. Monitor — Data structures

- [x] 1.1 Add `debug_psd_roll_[]` and `debug_psd_pitch_[]` members (512 floats each) to `monitor.hpp`, gated by `CONFIG_APP_DEBUG_CSV_LOGS`
- [x] 1.2 Add `debug_peaks_roll_` and `debug_peaks_pitch_` members (`PeakList` type) to `monitor.hpp`, gated by `CONFIG_APP_DEBUG_CSV_LOGS`
- [x] 1.3 Add `has_debug_analysis_data_` atomic bool flag and mutex-guarded getter methods to `monitor.hpp`

## 2. Monitor — Capture logic

- [x] 2.1 In `ComputeAxisNaturalFrequency()`, after normalizing `local_psd[]`, copy to the axis-specific `debug_psd_*_` member under mutex
- [x] 2.2 In `FindDecayRegion()`, after building `out_peaks`, copy peak amplitudes, times, and count to the axis-specific `debug_peaks_*_` member under mutex
- [x] 2.3 In `ComputeAndPublish()` when `is_exit=true`, set `has_debug_analysis_data_ = true` after both FFT calls complete

## 3. Logger — Storage layer

- [x] 3.1 Add `ResetDebugFftLog()` function to `logger_storage.cpp`: create/overwrite `debug_fft.csv` with header `timestamp_ms,axis,bin,freq_hz,psd_power`
- [x] 3.2 Add `ResetDebugPeaksLog()` function to `logger_storage.cpp`: create/overwrite `debug_peaks.csv` with header `timestamp_ms,axis,peak_idx,time_s,amplitude_deg,log_amplitude`
- [x] 3.3 Declare new storage functions in `logger_internal.hpp`

## 4. Logger — Formatting and flush

- [x] 4.1 Add `FormatAndWriteDebugFftCsv()` in `logger.cpp`: drain PSD data from monitor, format per-bin CSV rows, write to `debug_fft.csv` in a single fopen/fclose batch
- [x] 4.2 Add `FormatAndWriteDebugPeaksCsv()` in `logger.cpp`: drain peak data from monitor, format per-peak CSV rows, write to `debug_peaks.csv` in a single fopen/fclose batch
- [x] 4.3 In the logger task loop 1 Hz section, check `has_debug_analysis_data_` flag; if set, call both format+write functions and clear the flag

## 5. Integration

- [x] 5.1 Call `ResetDebugFftLog()` and `ResetDebugPeaksLog()` from logger init, alongside existing `ResetDebugLog()`
- [x] 5.2 Verify build compiles with both `CONFIG_APP_DEBUG_CSV_LOGS=y` and `=n`
