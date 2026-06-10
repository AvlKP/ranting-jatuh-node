## ADDED Requirements

### Requirement: Unsent MQTT payloads persist on SD
The system SHALL write all MQTT payloads to SD card files in the `outbox/pending/` directory before any MQTT publish attempt.

#### Scenario: Parameter payload persisted to SD
- **WHEN** the logger task formats a parameter JSON payload
- **THEN** it SHALL append the payload as a line to the current `pending/params_<epoch>.jsonl` file

#### Scenario: Failure payload persisted to SD
- **WHEN** the logger task formats a failure payload
- **THEN** it SHALL write the payload to an individual `pending/failure_<epoch>.jsonl` file

#### Scenario: New parameter file per publish period
- **WHEN** the current parameter file's epoch exceeds `CONFIG_LOGGER_WIFI_PERIOD_HOURS` since creation
- **THEN** the logger task SHALL create a new `pending/params_<epoch>.jsonl` file for subsequent payloads

### Requirement: Sent files are pruned
The system SHALL move successfully-published files to `outbox/sent/` and prune old sent files to prevent unbounded SD usage.

#### Scenario: Pruning old sent files
- **WHEN** the `sent/` directory contains more files than the configured retention limit (default: 10)
- **THEN** the oldest files in `sent/` SHALL be deleted until the count equals the retention limit

### Requirement: Outbox survives reboot
The system SHALL scan the `outbox/pending/` directory on boot and queue any existing files for upload.

#### Scenario: Pending files uploaded after reboot
- **WHEN** the network task starts and finds files in `outbox/pending/`
- **THEN** it SHALL publish those files before any newly-created files

### Requirement: SD outbox does not block logger task
All SD outbox write operations by the logger task SHALL complete within bounded time and SHALL NOT involve network I/O.

#### Scenario: Logger writes during network outage
- **WHEN** WiFi is unreachable
- **THEN** the logger task SHALL continue writing payloads to `outbox/pending/` without any delay or error related to network state
