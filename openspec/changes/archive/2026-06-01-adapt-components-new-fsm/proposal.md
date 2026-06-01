## Why

The recent introduction of the node state machine (`IDLE` vs `DISTURBED`) fundamentally changes how and when monitoring data is collected and processed. Existing components, particularly the `dashboard`, were designed assuming a continuous stream of data from a rolling 5-minute window. We need to adapt these downstream and related components to correctly interpret, display, and handle event-driven, aperiodic data triggered by the new FSM.

## What Changes

- Update `dashboard` component to handle aperiodic data instead of continuous data. It should reflect the node's current state (`IDLE` vs `DISTURBED`).
- Modify `logger` to handle state transition events and store/transmit them according to the new FSM rules, ensuring no data loss during state changes.
- Ensure any other data consumer components wait for or react to FSM triggers rather than polling continuously.

## Capabilities

### New Capabilities
- `dashboard-fsm-adaptation`: Updates the dashboard to correctly visualize event-driven parameter calculations and the current FSM state of the node.
- `logger-fsm-adaptation`: Updates the logger to handle event-based data saving and transmission linked to `DISTURBED` states.

### Modified Capabilities

## Impact

- `components/dashboard/dashboard.cpp`: Major logic changes to wait for state-triggered data.
- `components/logger/`: Updates to how logs are queued and sent via MQTT or saved to SD card based on the state.
- Data retention and MQTT reporting frequency will change from strictly periodic to event-based bursts.
