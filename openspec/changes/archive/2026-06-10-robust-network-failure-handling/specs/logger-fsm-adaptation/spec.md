## MODIFIED Requirements

### Requirement: Logger transmits event-driven parameters
The logger SHALL write parameter payloads to SD immediately and enqueue them for asynchronous MQTT publishing via the network task. The logger task SHALL NOT perform any WiFi or MQTT operations directly.

#### Scenario: Submitting parameters upon DISTURBED exit
- **WHEN** the node transitions from `DISTURBED` to `IDLE`
- **THEN** the logger writes the parameter payload to SD
- **THEN** the logger appends the JSON payload to the SD outbox for the network task to publish

#### Scenario: Submitting parameters on refresh
- **WHEN** the node is in prolonged `DISTURBED` state and emits a refresh parameter block
- **THEN** the logger writes the parameter payload to SD
- **THEN** the logger appends the JSON payload to the SD outbox for the network task to publish

#### Scenario: Submitting IDLE window parameters
- **WHEN** the monitor emits an `IDLE` parameter block
- **THEN** the logger writes the parameter payload to SD
- **THEN** the logger appends the JSON payload to the SD outbox for the network task to publish

## ADDED Requirements

### Requirement: Logger writes failure events to SD before network
The logger SHALL write failure events to SD storage unconditionally and immediately, before any network publish attempt is enqueued.

#### Scenario: Failure event SD write ordering
- **WHEN** the logger receives a failure event (free fall or acoustic emission)
- **THEN** it SHALL write the failure CSV to SD storage first
- **THEN** it SHALL write the failure JSON to the SD outbox for the network task to publish

#### Scenario: Failure SD write during network outage
- **WHEN** the logger receives a failure event and WiFi is unreachable
- **THEN** the failure SHALL be written to SD within one task loop tick (~100ms)
- **THEN** the failure SHALL NOT be delayed by any network timeout or retry
