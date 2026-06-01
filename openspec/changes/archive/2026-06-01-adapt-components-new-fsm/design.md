## Context

The newly introduced node FSM (`IDLE` vs `DISTURBED`) changes parameter generation from continuous 5-minute periodic to event-driven when a disturbance occurs. The `dashboard` and `logger` components must be updated to expect aperiodic parameters. The dashboard previously expected continuous periodic data.

## Goals / Non-Goals

**Goals:**
- Update `logger` to batch and transmit data based on event-driven availability rather than fixed polling.
- Update `dashboard` to parse, render, and react to event-based data correctly.

**Non-Goals:**
- Completely rewriting the MQTT message payload format.
- Adding historical data retention beyond what the logger already supports.

## Decisions

**1. Logger Event-Driven Processing:**
Instead of a periodic timer triggering MQTT send, the logger will observe state transitions. When the state machine emits a parameter block (either a refresh during `DISTURBED` or upon entering `IDLE`), the logger will queue and send the payload.
*Rationale:* Ensures timely delivery of disturbance data without relying on a slow periodic timer.

**2. Dashboard Aperiodic Rendering:**
The dashboard UI will be updated to display the node's current FSM state and dynamically update views when new event-driven payloads arrive, interpreting long gaps as `IDLE` periods.
*Rationale:* Reflects the true state of the node instead of showing stale data during idle periods.

**3. Monitor Short Buffer Resilience:**
`ComputeAndPublish` must not abort if `ComputeNaturalFrequency` or `ComputeSwayAndDamping` fails. For short disturbances (<1024 samples), the FFT is skipped and frequency is reported as 0Hz. For extremely short disturbances (<3 peaks), damping is reported as 0. The basic stats (mean, variance) will still be published to the logger and dashboard.
*Rationale:* Ensures short but significant disturbances are recorded rather than silently dropped.

## Risks / Trade-offs

- **Risk:** Dropped MQTT messages might hide disturbances. 
  - **Mitigation:** The logger already implements local SD card backup. Ensure QoS 1 is maintained for critical failure events.
