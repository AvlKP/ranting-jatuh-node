## MODIFIED Requirements

### Requirement: CSV Debug Log Formatting
The system SHALL format the raw acceleration and tilt data as comma-separated values and buffer them in memory before writing to the CSV file. Each row SHALL include the current node state as an integer column (0=IDLE, 1=DISTURBED).

#### Scenario: Buffering data row
- **WHEN** new acceleration and tilt data is available
- **THEN** the system formats a row containing timestamp, accel_x, accel_y, accel_z, tilt_x, tilt_y, tilt_z, state and appends it to an in-memory ring buffer without writing to the CSV file immediately

## ADDED Requirements

### Requirement: State Column in StreamSample
The system SHALL include the current node state in each stream sample sent for debug logging.

#### Scenario: Monitor includes state
- **WHEN** the monitor builds a StreamSample for debug logging
- **THEN** the sample includes a state field reflecting the current node state (IDLE or DISTURBED) as an integer value

### Requirement: Batched CSV Flush
The system SHALL flush buffered CSV debug log lines to the SD card file at a target interval of 1 Hz (once per second).

#### Scenario: Periodic flush
- **WHEN** at least 1 second has elapsed since the last flush and buffered lines exist
- **THEN** the system opens the debug CSV file once, writes all buffered lines in sequence, and closes the file

#### Scenario: Buffer-full early flush
- **WHEN** the ring buffer reaches capacity before the 1-second interval elapses
- **THEN** the system flushes all buffered lines to the CSV file immediately to prevent data loss

#### Scenario: Shutdown flush
- **WHEN** the system is shutting down or the logger task is terminating
- **THEN** any remaining buffered debug log lines SHALL be flushed to the CSV file before the logger module finalizes
