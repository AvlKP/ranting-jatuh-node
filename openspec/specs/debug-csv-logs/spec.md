# debug-csv-logs Specification

## Purpose
Defines the CSV debug logging system for raw acceleration and tilt data, including configuration, ring-buffer delivery from monitor to logger, formatting, batched SD card writes, and exclusion from server transmission.
## Requirements
### Requirement: CSV Debug Logging Configuration
The system SHALL provide a configuration option to enable or disable all CSV debug logging, including stream-sample CSV, FFT PSD CSV, and peak envelope CSV.

#### Scenario: System boots with CSV debug logging enabled
- **WHEN** the system initializes with the CSV debug logging configuration set to true
- **THEN** the system prepares to log raw acceleration, tilt, FFT, and peak envelope data to their respective CSV files

#### Scenario: System boots with CSV debug logging disabled
- **WHEN** the system initializes with the CSV debug logging configuration set to false
- **THEN** the system SHALL NOT log any debug CSV data and SHALL NOT allocate debug logging resources

### Requirement: CSV Debug Log Formatting
The system SHALL format the raw acceleration and tilt data as comma-separated values and buffer them in memory before writing to the CSV file. Each row SHALL include the current node state as an integer column (0=IDLE, 1=DISTURBED). Stream sample data SHALL be delivered from the monitor to the logger via a lock-free atomic ring buffer, NOT through the esp_event system.

#### Scenario: Buffering data row
- **WHEN** new acceleration and tilt data is available
- **THEN** the system formats a row containing timestamp, accel_x, accel_y, accel_z, tilt_x, tilt_y, tilt_z, state and appends it to an in-memory ring buffer without writing to the CSV file immediately

#### Scenario: Monitor writes stream samples to ring buffer
- **WHEN** the monitor acquires a new IMU sample
- **THEN** the monitor writes a StreamSample entry to the dedicated debug ring buffer using atomic indices, without posting any esp_event

#### Scenario: Logger polls ring buffer
- **WHEN** the logger task loop performs its 1-second debug flush check
- **THEN** the logger reads all accumulated StreamSample entries from the ring buffer in batches of up to 32, formats them as CSV rows, and appends them to the storage ring buffer for batched write

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

### Requirement: State Column in StreamSample
The system SHALL include the current node state in each stream sample stored in the debug ring buffer.

#### Scenario: Monitor includes state
- **WHEN** the monitor writes a StreamSample to the debug ring buffer
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

