# network-task Specification

## Purpose
TBD - created by archiving change robust-network-failure-handling. Update Purpose after archive.
## Requirements
### Requirement: Network task runs independently from logger task
The system SHALL run all WiFi, NTP, and MQTT operations on a dedicated FreeRTOS task (`network_task`) separate from the logger task.

#### Scenario: Logger task not blocked by network
- **WHEN** the network task is attempting a WiFi connect that takes 20 seconds
- **THEN** the logger task SHALL continue processing sensor events and writing to SD without delay

#### Scenario: Network task receives publish requests
- **WHEN** the logger task enqueues a publish request
- **THEN** the network task SHALL dequeue and attempt MQTT publish on its own thread

### Requirement: Network task publishes from SD outbox
The network task SHALL scan the SD outbox `pending/` directory for unsent files and publish their contents via MQTT.

#### Scenario: Publishing pending files on reconnect
- **WHEN** the network task establishes a WiFi+MQTT connection after a period of disconnection
- **THEN** it SHALL scan `pending/` for files and publish each file's contents line-by-line to the appropriate MQTT topic

#### Scenario: Failure files published before parameters
- **WHEN** the network task scans `pending/` and finds both `failure_*` and `params_*` files
- **THEN** it SHALL publish all `failure_*` files before any `params_*` files

#### Scenario: File moved to sent after successful publish
- **WHEN** the network task successfully publishes all lines from a pending file
- **THEN** it SHALL move the file from `pending/` to `sent/`

### Requirement: Network task implements exponential backoff
The network task SHALL implement exponential backoff on WiFi or MQTT connection failure to conserve power.

#### Scenario: Initial backoff on first failure
- **WHEN** a WiFi connect or MQTT connect attempt fails for the first time
- **THEN** the network task SHALL wait at least 60 seconds before the next attempt

#### Scenario: Doubling backoff on repeated failures
- **WHEN** consecutive connection attempts fail
- **THEN** the backoff interval SHALL double after each failure, up to a maximum of 1 hour

#### Scenario: Backoff reset on success
- **WHEN** a connection attempt succeeds after previous failures
- **THEN** the backoff interval SHALL reset to 60 seconds

#### Scenario: No publish attempts during backoff
- **WHEN** the backoff timer has not expired
- **THEN** the network task SHALL NOT attempt any WiFi or MQTT connection

### Requirement: Network task manages MQTT client lifecycle
The network task SHALL create, configure, and destroy the MQTT client handle. All failure paths after `esp_mqtt_client_init()` SHALL destroy the handle and reset it to null.

#### Scenario: Client handle cleanup on property failure
- **WHEN** `esp_mqtt5_client_set_connect_property()` fails after client init
- **THEN** the network task SHALL call `esp_mqtt_client_destroy()` and set the handle to null

#### Scenario: Client handle cleanup on event registration failure
- **WHEN** `esp_mqtt_client_register_event()` fails after client init
- **THEN** the network task SHALL call `esp_mqtt_client_destroy()` and set the handle to null

#### Scenario: Subsequent publish after failed init
- **WHEN** a previous client init failed and was cleaned up
- **THEN** the next publish attempt SHALL re-create the client from scratch without crashing

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

### Requirement: Failure-triggered publish cycles SHALL respect parameter cadence
An urgent failure publish cycle SHALL upload failure files before the parameter cadence expires, but SHALL NOT upload parameter files early.

#### Scenario: Failure and sealed parameters pending before cadence
- **WHEN** one or more failure files are pending
- **AND** one or more sealed parameter files are pending
- **AND** the parameter upload cadence is not due
- **THEN** the network task SHALL publish eligible failure files only
- **AND** it SHALL leave sealed parameter files pending for a later cadence-due cycle

#### Scenario: Failure and sealed parameters pending after cadence
- **WHEN** one or more failure files are pending
- **AND** one or more sealed parameter files are pending
- **AND** the parameter upload cadence is due
- **THEN** the network task SHALL publish failure files first
- **AND** it MAY publish sealed parameter files in the same successful network cycle

### Requirement: Network publish path SHALL remain stack-safe under multi-file batches
The network task SHALL survive worst-case configured pending-file batches without stack canary panic or stack overflow.

#### Scenario: Multi-file publish batch
- **WHEN** the pending outbox contains failure files and the maximum configured number of sealed parameter files returned by the scanner
- **AND** WiFi, SNTP, MQTT5 connect, line-by-line publish, sent moves, pruning, and disconnect all execute
- **THEN** `network_task` SHALL remain alive without stack overflow
- **AND** verification SHALL record network task stack high-water margin

#### Scenario: Large local buffers audited
- **WHEN** network publish code needs filename arrays, path buffers, MQTT line buffers, or publish property structs
- **THEN** recurring task-path storage SHALL use bounded static task-owned storage, smaller chunked buffers, or a stack size with measured margin
- **AND** large automatic buffers SHALL be justified by recorded high-water margin

### Requirement: Runtime publish diagnostics SHALL classify real and benign errors
Network verification logs SHALL distinguish fatal publish errors from benign ESP-IDF debug probes.

#### Scenario: SDMMC IO-card probe fails during SD-card init
- **WHEN** ESP-IDF logs `sdmmc_req` or `sdmmc_io` debug messages indicating an IO-card probe returned `0x107`
- **AND** SD card initialization continues and mounts successfully
- **THEN** verification SHALL classify the messages as benign debug probes
- **AND** they SHALL NOT fail outbox runtime verification

#### Scenario: Outbox sent rename reports destination exists
- **WHEN** outbox sent transition encounters a destination-exists condition after successful MQTT publish
- **THEN** diagnostics SHALL identify it as an outbox collision defect unless collision-safe handling succeeds
- **AND** verification SHALL fail if the file remains in pending due to the collision

