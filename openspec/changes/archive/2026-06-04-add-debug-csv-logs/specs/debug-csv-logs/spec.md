## ADDED Requirements

### Requirement: CSV Debug Logging Configuration
The system SHALL provide a configuration option to enable or disable CSV debug logging for acceleration and tilt data.

#### Scenario: System boots with CSV debug logging enabled
- **WHEN** the system initializes with the CSV debug logging configuration set to true
- **THEN** the system prepares to log raw acceleration and tilt data to a CSV file

### Requirement: CSV Debug Log Formatting
The system SHALL format the raw acceleration and tilt data as comma-separated values.

#### Scenario: Writing data row
- **WHEN** new acceleration and tilt data is available
- **THEN** the system writes a row containing timestamp, accel_x, accel_y, accel_z, tilt_x, tilt_y, tilt_z to the CSV file

### Requirement: CSV Log File Overwrite
The system SHALL rewrite (overwrite) the CSV debug log file at the beginning of each run.

#### Scenario: Starting a new run
- **WHEN** the system initializes and CSV debug logging is enabled
- **THEN** the previous contents of the CSV debug log file are cleared

### Requirement: Exclude Debug Logs from Server Transmission
The system SHALL NOT transmit CSV debug logs to the server.

#### Scenario: Attempting to send logs
- **WHEN** the system processes logs for server transmission
- **THEN** the system ensures that the CSV debug logs are excluded from the payload
