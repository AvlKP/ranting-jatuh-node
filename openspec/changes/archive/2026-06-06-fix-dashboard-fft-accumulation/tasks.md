## 1. Monitor Fix

- [x] 1.1 In `ComputeAndPublish()`, add `psd_accum_.fill(0.0f)` at the start of the `if (is_exit)` block before the FFT computation calls
- [x] 1.2 Verify build compiles with `idf.py build`

## 2. Verify Dashboard Behavior

- [x] 2.1 Flash firmware, trigger two distinct branch disturbances at different frequencies
- [x] 2.2 Verify dashboard FFT chart shows only the most recent event's spectrum
- [x] 2.3 Verify that after the second event, the first event's frequency peak is no longer visible
- [x] 2.4 Verify computed `natural_freq_hz` in the parameter CSV is still correct
