## ADDED Requirements

### Requirement: README reflects current project state

The README.md SHALL document the project's purpose, target hardware, system architecture, build instructions, configuration, current implementation status, and planned future work.

#### Scenario: New developer reads README

- **WHEN** a developer unfamiliar with the project opens README.md
- **THEN** they can identify the project purpose (tree branch monitoring system), target hardware (ESP32-S3 with custom PCB), and key components (IMU, logger, monitor, dashboard)
- **AND** they can follow build instructions to compile and flash firmware

#### Scenario: README lists hardware requirements

- **WHEN** a developer reads the hardware section
- **THEN** they find the required MCU (ESP32-S3FH4R2), sensors (LSM6DS3 IMU, acoustic emission sensor), storage (microSD via SDIO 1-bit), and communication (WiFi, MQTT)

#### Scenario: README describes architecture

- **WHEN** a developer reads the architecture section
- **THEN** they understand the component structure (monitor processes sensor data, logger writes to SD and transmits via MQTT, lsm6ds3 handles IMU communication, dashboard serves web interface)

#### Scenario: README lists configuration options

- **WHEN** a developer reads the configuration section
- **THEN** they find key Kconfig options (WiFi credentials, MQTT broker URI, sample rates, MQTT topics, node ID) and how to configure them via `idf.py menuconfig`

#### Scenario: README shows implementation status

- **WHEN** a developer reads the implementation status section
- **THEN** they can see what features are complete (RTOS event-driven architecture, IMU driver, FFT analysis, damping ratio calculation, MQTT publishing, SD logging, web dashboard, OTA) and what remains (wind/storm state detection, IMU FIFO utilization, temperature calibration)

### Requirement: README does not contain stale information

The README SHALL NOT contain the old TODO list format. All information SHALL be current and accurate.

#### Scenario: Old TODO list is gone

- **WHEN** any developer opens README.md
- **THEN** the old numbered TODO list starting with "1. Transition to RTOS and esp_event.h API [x]" does not appear

#### Scenario: Completed items are documented as complete

- **WHEN** a developer reads about RTOS/event-driven architecture
- **THEN** it is presented as a completed implementation detail, not a pending task

#### Scenario: Remaining work is in future work section

- **WHEN** a developer reads the future work section
- **THEN** they find pending items (wind/storm state, IMU FIFO, temperature calibration) labeled as future enhancements, not as stale TODOs
