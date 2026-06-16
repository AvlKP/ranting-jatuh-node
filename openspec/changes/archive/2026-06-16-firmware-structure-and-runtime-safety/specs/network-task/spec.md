## ADDED Requirements

### Requirement: Parameter uploads SHALL honor configured cadence in on-demand mode
In on-demand field mode, parameter-only outbox data SHALL NOT cause WiFi or MQTT activity before `CONFIG_LOGGER_WIFI_PERIOD_HOURS` has elapsed since the previous parameter upload cycle.

#### Scenario: Parameter data pending before period expires
- **WHEN** the system is built with on-demand WiFi strategy
- **AND** only parameter data is pending
- **AND** the configured publish period has not elapsed
- **THEN** the network task SHALL NOT start WiFi
- **AND** it SHALL leave active parameter data on SD

#### Scenario: Parameter period expires
- **WHEN** the system is built with on-demand WiFi strategy
- **AND** parameter data is pending
- **AND** the configured publish period has elapsed
- **AND** network backoff is not active
- **THEN** the network task SHALL seal eligible parameter files
- **AND** it SHALL attempt WiFi and MQTT upload

### Requirement: Failure uploads SHALL be urgent when backoff allows
Failure files SHALL be eligible for upload before the parameter publish period expires, subject to network backoff.

#### Scenario: Failure file pending before parameter period expires
- **WHEN** a failure file exists in `outbox/pending/`
- **AND** network backoff is not active
- **THEN** the network task SHALL attempt a publish cycle without waiting for the parameter publish period
- **AND** failure files SHALL be published before parameter files

#### Scenario: Failure file pending during backoff
- **WHEN** a failure file exists in `outbox/pending/`
- **AND** network backoff is active
- **THEN** the network task SHALL NOT attempt WiFi or MQTT connection until backoff expires

### Requirement: Network notifications SHALL be wake hints
Logger notifications SHALL wake the network task but SHALL NOT force an unconditional publish attempt.

#### Scenario: Parameter append sends notification
- **WHEN** the logger appends parameter data and notifies the network task
- **AND** no failure file exists
- **AND** the parameter publish period has not elapsed
- **THEN** the network task SHALL return to waiting without starting WiFi

#### Scenario: Failure append sends notification
- **WHEN** the logger appends failure data and notifies the network task
- **AND** backoff is not active
- **THEN** the network task SHALL treat the notification as an urgent publish wake

### Requirement: Publish-cycle diagnostics SHALL distinguish skip reasons
The network task SHALL expose or log why a publish cycle was skipped.

#### Scenario: Publish skipped by cadence
- **WHEN** the network task wakes with parameter-only data before the publish period expires
- **THEN** diagnostics SHALL indicate that upload was skipped because the parameter cadence was not due

#### Scenario: Publish skipped by backoff
- **WHEN** the network task wakes while backoff is active
- **THEN** diagnostics SHALL indicate that upload was skipped because backoff was active
