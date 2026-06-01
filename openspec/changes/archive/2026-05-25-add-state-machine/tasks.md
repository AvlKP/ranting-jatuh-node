## 1. Monitor State Setup & Configuration

- [x] 1.1 Add Kconfig parameters to `Kconfig.projbuild`: `CONFIG_MONITOR_SHORT_BUFFER_SIZE`, `CONFIG_MONITOR_K_IDLE_X100`, `CONFIG_MONITOR_K_DISTURBED_X100`, `CONFIG_MONITOR_N_DPAD`.
- [x] 1.2 Define `NodeState` enum (`IDLE`, `DISTURBED`) in the monitor component header.
- [x] 1.3 Add `std::array` for short buffers and tracking variables for rolling variance sums (sum, sum of squares) in `monitor.hpp`.
- [x] 1.4 Initialize node state to `IDLE` in the monitor startup sequence.

## 2. Live Variance & State Logic

- [x] 2.1 Update `Monitor::PushSample()` to compute an O(1) rolling variance for the short buffer.
- [x] 2.2 Implement `IDLE` -> `DISTURBED` state transition when short variance > `K_IDLE` * IDLE 5-min variance.
- [x] 2.3 On `IDLE` -> `DISTURBED` transition, implement `std::copy` of the short buffer into the main 5-min buffer.
- [x] 2.4 Add condition to block `IDLE` -> `DISTURBED` transition if the first 5-minute baseline variance is not yet calculated (cold start).

## 3. Parameter Calculation Isolation & Double Trigger

- [x] 3.1 Implement `DISTURBED` -> `IDLE` transition when short variance < `K_DISTURBED` * IDLE 5-min variance.
- [x] 3.2 Implement `DISTURBED` -> `DISTURBED` refresh transition when buffer is `N_DPAD` samples away from being full.
- [x] 3.3 Ensure both transitions trigger calculations for natural frequency, damping ratio, SAMEAN, and SAMAX using the DISTURBED 5-min buffer.
- [x] 3.4 Ensure the parameter calculation results are sent to MQTT immediately upon both transitions.

## 4. IDLE State Maintenance

- [x] 4.1 Update `Monitor::ComputeStats()` to run strictly over the 5-minute window for the `IDLE` state.
- [x] 4.2 Save the calculated 5-minute IDLE variance for threshold comparison against the short buffer.
