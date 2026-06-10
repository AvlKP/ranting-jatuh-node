## 1. Baseline Verification

- [x] 1.1 Run `idf.py build` to establish baseline compilation success before any removals

## 2. Remove Dead Short Buffer Running Sums

- [x] 2.1 Grep `roll_short_sum_`, `roll_short_sq_sum_`, `pitch_short_sum_`, `pitch_short_sq_sum_` -- confirm written but never read for computation
- [x] 2.2 Remove `roll_short_sum_`, `roll_short_sq_sum_`, `pitch_short_sum_`, `pitch_short_sq_sum_` from monitor.hpp (lines 408-411)
- [x] 2.3 Remove sum maintenance in PushSample(): subtraction of old values (lines 432-435) and addition of new values (lines 449-452)
- [x] 2.4 Run `idf.py build` to confirm short buffer sum removal doesn't break compilation

## 3. Remove Taring from monitor.hpp

- [x] 3.1 Remove taring member variables: `taring_complete_`, `roll_offset_`, `pitch_offset_`, `roll_tare_sum_`, `pitch_tare_sum_`, `tare_samples_accumulated_`, `tare_settle_accumulated_`
- [x] 3.2 Run `idf.py build` to confirm header changes don't break compilation

## 4. Remove Taring from monitor.cpp

- [x] 4.1 Remove `#if CONFIG_MONITOR_TARE_ENABLE` guard and entire taring block in `Update()` (lines ~262-294) -- keep only `current_roll = filter_.roll(); current_pitch = filter_.pitch();`
- [x] 4.2 Remove taring member initialization in `Init()` (lines 187-197): `taring_complete_`, `roll_offset_`, `pitch_offset_`, tare sums, counters
- [x] 4.3 Run `idf.py build` to confirm taring removal doesn't break compilation

## 5. Remove Taring Kconfig Keys

- [x] 5.1 Remove `CONFIG_MONITOR_TARE_ENABLE` from `components/monitor/Kconfig`
- [x] 5.2 Remove `CONFIG_MONITOR_TARE_SAMPLES` from `components/monitor/Kconfig`
- [x] 5.3 Remove `CONFIG_MONITOR_TARE_SETTLE_SAMPLES` from `components/monitor/Kconfig`
- [x] 5.4 Remove `CONFIG_MONITOR_TARE_ENABLE=y` from `sdkconfig.defaults` if present
- [x] 5.5 Remove `CONFIG_MONITOR_TARE_SAMPLES=100` from `sdkconfig.defaults` if present
- [x] 5.6 Run `idf.py build` to confirm Kconfig removal doesn't break menuconfig or compilation

## 6. Final Verification

- [x] 6.1 Run `idf.py build` -- confirm clean compilation with zero taring code
- [x] 6.2 Grep for removed identifiers (`taring_complete_`, `roll_offset_`, `pitch_offset_`, `roll_tare_sum_`, `pitch_tare_sum_`, `tare_samples_accumulated_`, `tare_settle_accumulated_`, `roll_short_sum_`, `roll_short_sq_sum_`, `pitch_short_sum_`, `pitch_short_sq_sum_`, `CONFIG_MONITOR_TARE_ENABLE`, `CONFIG_MONITOR_TARE_SAMPLES`, `CONFIG_MONITOR_TARE_SETTLE_SAMPLES`, `MONITOR_TARE`) to confirm zero remaining references across `.cpp`/`.hpp`/`Kconfig`/`.defaults`
- [x] 6.3 Verify short buffer arrays still exist: `roll_short_`, `pitch_short_`, `gmag_short_`, `gx_short_`, `gy_short_`, `gz_short_`, `ax_short_`, `ay_short_`, `az_short_`

## 7. Documentation and Config Cleanup

- [x] 7.1 Update `StreamSample` docstring in `components/monitor/include/monitor.hpp` -- change "Tared roll angle" and "Tared pitch angle" to "Roll angle" and "Pitch angle"
- [x] 7.2 Update `MONITOR_SHORT_BUFFER_SIZE` help text and prompt in `components/monitor/Kconfig` -- change prompt to "Short buffer size for pre-disturbance history" and replace help text reference to variance calculations with pre-disturbance history backfill
- [x] 7.3 Update taring algorithm documentation in `notebook/NOTES.md` -- mark section 2 (Taring Algorithm) as deprecated/removed
