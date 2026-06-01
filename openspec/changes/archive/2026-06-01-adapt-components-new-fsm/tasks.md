## 1. Logger FSM Integration

- [x] 1.1 Subscribe logger component to node FSM state transition events
- [x] 1.2 Refactor logger to publish MQTT parameters on `DISTURBED` -> `IDLE` state transition instead of relying on continuous periodic timers
- [x] 1.3 Add logic to logger to publish parameters on prolonged `DISTURBED` refresh events

## 2. Dashboard Aperiodic Data Handling

- [x] 2.1 Update `dashboard.cpp` state management to handle `IDLE` and `DISTURBED` node states
- [x] 2.2 Modify dashboard data rendering logic to update on event-driven data instead of expecting continuous periodic updates
- [x] 2.3 Add UI/status indicators in dashboard to show current node FSM state clearly

## 3. Monitor Fallback Fixes

- [x] 3.1 Modify `ComputeAndPublish` to continue executing instead of aborting when a calculation step returns false
- [x] 3.2 Update `ComputeNaturalFrequency` and `ComputeSwayAndDamping` to cleanly fallback (zeroing fields) without crashing
- [x] 3.3 Ensure downstream consumers correctly interpret 0 values for frequency/damping as "insufficient data"
