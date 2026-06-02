# Fix Taring Convergence

## Problem
The dashboard shows non-zero steady-state values (e.g. roll=2.0, pitch=-3.0) even after taring completes. This happens because taring starts immediately at boot, averaging the first 100 samples (1 second at 100Hz). However, the Madgwick/Complementary filter takes time to converge to the true tilt from its initial state. By taring while the filter is still converging, `roll_offset_` captures an intermediate value rather than the true steady-state offset, resulting in a persistent error once the filter fully settles.

## Proposed Solution
Delay the taring accumulation until the IMU filter has sufficiently converged. 
1. Introduce a settling time mechanism before taring starts.
2. We can add a configuration `CONFIG_MONITOR_TARE_SETTLE_SAMPLES` or simply wait a predefined number of samples (e.g., 500 samples for ~5 seconds at 100Hz) before starting the taring process.
3. Only begin accumulating `roll_tare_sum_` and `pitch_tare_sum_` after this settling period has passed.

## Scope
- `components/monitor/monitor.cpp`: Update `Monitor::Update` to implement the settling delay before accumulating tare values.
- `components/monitor/include/monitor.hpp`: Add a `tare_settle_samples_accumulated_` counter or reuse existing states.
- `components/monitor/Kconfig`: Add a new `CONFIG_MONITOR_TARE_SETTLE_SAMPLES` parameter to make the settling time configurable.
