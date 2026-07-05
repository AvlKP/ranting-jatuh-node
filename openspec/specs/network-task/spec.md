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
The network task SHALL implement exponential backoff on connection failure to bound reconnect load. In on-demand mode the backoff applies to WiFi or MQTT connection failures. In persistent mode the backoff applies to MQTT connection failures only, because WiFi reconnect backoff is owned by the persistent network strategy.

#### Scenario: Initial backoff on first failure
- **WHEN** a connection attempt fails for the first time
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

#### Scenario: Persistent mode does not double-count WiFi reconnect
- **WHEN** the persistent strategy is retrying WiFi reconnect on its own backoff schedule
- **THEN** the network task SHALL NOT also apply its own backoff to the same WiFi disconnect
- **AND** the network task backoff SHALL apply only when an MQTT connect attempt fails

### Requirement: Network task manages MQTT client lifecycle
The network task SHALL create, configure, and destroy the MQTT client handle. All failure paths after `esp_mqtt_client_init()` SHALL destroy the handle and reset it to null. In persistent mode the MQTT client SHALL be configured with `disable_auto_reconnect = false` and a bounded reconnect interval, and the task SHALL NOT destroy or stop the client across publish cycles.

#### Scenario: Client handle cleanup on property failure
- **WHEN** `esp_mqtt5_client_set_connect_property()` fails after client init
- **THEN** the network task SHALL call `esp_mqtt_client_destroy()` and set the handle to null

#### Scenario: Client handle cleanup on event registration failure
- **WHEN** `esp_mqtt_client_register_event()` fails after client init
- **THEN** the network task SHALL call `esp_mqtt_client_destroy()` and set the handle to null

#### Scenario: Subsequent publish after failed init
- **WHEN** a previous client init failed and was cleaned up
- **THEN** the next publish attempt SHALL re-create the client from scratch without crashing

#### Scenario: Persistent mode keeps MQTT client alive across cycles
- **WHEN** the system is built with `CONFIG_LOGGER_NETWORK_MODE=persistent`
- **AND** a publish cycle completes successfully
- **THEN** the network task SHALL NOT call `esp_mqtt_client_stop()` or `esp_mqtt_client_destroy()`
- **AND** the next publish cycle SHALL reuse the existing client

#### Scenario: Persistent mode enables MQTT auto-reconnect
- **WHEN** the MQTT client is created in persistent mode
- **THEN** `disable_auto_reconnect` SHALL be set to `false`
- **AND** a bounded reconnect interval SHALL be configured on the client

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

### Requirement: Persistent publish cycle SHALL reuse the WiFi link
In persistent mode the network task SHALL NOT tear down WiFi or MQTT between publish cycles. `network::ReleaseConnection()` SHALL remain a no-op for the persistent strategy, and the task SHALL NOT call it expecting WiFi shutdown.

#### Scenario: No WiFi teardown after publish in persistent mode
- **WHEN** a publish cycle completes in persistent mode
- **THEN** the network task SHALL NOT disconnect WiFi
- **AND** `network::IsConnected()` SHALL remain true

#### Scenario: WiFi stays associated between cadence waits
- **WHEN** the network task is waiting for the next publish cadence in persistent mode
- **THEN** WiFi SHALL remain associated and the MQTT client SHALL remain started

### Requirement: Every reconnect occurrence SHALL be enumerated and bounded
The system SHALL identify and bound every reconnect occurrence: boot initial connect, WiFi mid-session disconnect, WiFi `EnsureConnected` failure, MQTT mid-session disconnect, and MQTT connect timeout. Each occurrence SHALL have a single defined backoff owner (persistent WiFi strategy or network task) and a bounded maximum retry interval.

#### Scenario: Boot initial connect
- **WHEN** the system boots in persistent mode
- **THEN** the first WiFi connect SHALL be attempted with the initial backoff interval
- **AND** failure SHALL trigger the persistent WiFi backoff schedule

#### Scenario: WiFi mid-session disconnect
- **WHEN** `WIFI_EVENT_STA_DISCONNECTED` fires after IP was acquired in persistent mode
- **THEN** the persistent WiFi strategy SHALL own the reconnect with its bounded backoff
- **AND** the network task SHALL NOT start its own WiFi connect

#### Scenario: WiFi EnsureConnected failure
- **WHEN** `EnsureConnected()` returns false in persistent mode because the backoff window has not elapsed
- **THEN** the network task SHALL treat the publish cycle as failed
- **AND** it SHALL NOT reset or duplicate the WiFi backoff timer

#### Scenario: MQTT mid-session disconnect
- **WHEN** `MQTT_EVENT_DISCONNECTED` fires in persistent mode
- **THEN** the ESP-IDF MQTT client SHALL auto-reconnect using its configured bounded interval
- **AND** the network task SHALL NOT destroy and recreate the client

#### Scenario: MQTT connect timeout in task loop
- **WHEN** the network task waits for `MQTT_EVENT_CONNECTED` and times out in persistent mode
- **THEN** the network task SHALL apply its own exponential backoff
- **AND** it SHALL NOT tear down WiFi

