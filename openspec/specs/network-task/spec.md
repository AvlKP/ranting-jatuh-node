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

