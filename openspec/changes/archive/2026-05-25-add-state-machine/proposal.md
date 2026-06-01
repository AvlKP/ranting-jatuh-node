## Why

The current implementation calculates natural frequency, damping ratio, and sway amplitude using a continuous 5-minute time window. Since there is no disturbance most of the time, this continuous calculation over mostly quiet periods results in a DC bias in the parameters. Introducing a state machine allows the node to only process data when an actual disturbance occurs.

## What Changes

- Implement a state machine for the monitoring node with two primary states: `IDLE` and `DISTURBED`.
- Node stays in `IDLE` during normal, quiet periods without performing the 5-minute parameter calculations.
- Node transitions to `DISTURBED` state when a disturbance is detected (e.g., via the Acoustic Emission sensor or IMU thresholds).
- Parameter calculations (natural frequency, damping ratio, sway amplitude) are only performed during the `DISTURBED` state.

## Capabilities

### New Capabilities
- `node-state-machine`: Defines the states (`IDLE`, `DISTURBED`) and transition logic for the IoT monitoring node.

### Modified Capabilities

## Impact

- `monitor` component: Will need to integrate state transition logic and conditionally trigger calculations.
- Sensor data processing pipeline: Data windowing will change from continuous 5-minute rolling windows to event-driven windows triggered by the state machine.
