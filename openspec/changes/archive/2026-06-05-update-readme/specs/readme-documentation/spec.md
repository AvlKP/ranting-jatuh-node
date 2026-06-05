## ADDED Requirements

### Requirement: README reflects current project state
The README.md SHALL describe the project's purpose, architecture, hardware requirements, build instructions, configuration, and current implementation status.

#### Scenario: Developer reads README for first time
- **WHEN** a developer opens README.md for the first time
- **THEN** they understand what the project does, its architecture, and how to build and run it

#### Scenario: README documents hardware requirements
- **WHEN** a developer reads the README
- **THEN** they find the target hardware (ESP32-S3 on custom PCB) and required sensors (LSM6DS3 IMU, AE sensor, microSD)

#### Scenario: README documents build instructions
- **WHEN** a developer wants to build the project
- **THEN** the README provides the ESP-IDF version requirement, setup commands, and build/flash commands

#### Scenario: README documents future work
- **WHEN** a developer wants to understand the roadmap
- **THEN** the README lists remaining TODO items (FIFO usage, temp calibration, wind state)

### Requirement: README does not contain stale information
The README.md SHALL NOT contain the old TODO list format.

#### Scenario: Old content removed
- **WHEN** the README is updated
- **THEN** the numbered TODO list items ("1. Transition to RTOS...", etc.) are no longer present
