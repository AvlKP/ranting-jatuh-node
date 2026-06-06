# sd-card-resilience Specification

## Purpose
Defines SD card health detection and graceful degradation when the SD card becomes unresponsive, preventing system crashes and watchdog resets from blocking I/O operations.

## Requirements
### Requirement: SD Card Health Detection
The system SHALL detect when the SD card becomes unresponsive by monitoring SDMMC I/O operation results. When SD operations fail with timeout or I/O errors, the system SHALL mark the card as unhealthy and skip subsequent SD operations for a cooldown period.

#### Scenario: SD card read timeout during directory listing
- **WHEN** the dashboard StatusHandler calls opendir() and the underlying SDMMC read returns a timeout error (0x107)
- **THEN** the system sets the SD health flag to unhealthy and returns an empty file list without retrying

#### Scenario: SD card write failure in logger
- **WHEN** the logger attempts AppendParameter() and fopen() or fwrite() returns an I/O error
- **THEN** the system sets the SD health flag to unhealthy and the logger skips the write operation

#### Scenario: SD health cooldown expiration
- **WHEN** the SD health flag has been unhealthy for 5 seconds with no new errors
- **THEN** the system resets the health flag to healthy and resumes SD operations on the next attempt

### Requirement: Graceful Degradation of SD-Dependent Features
When the SD card is marked unhealthy, the dashboard SHALL continue serving HTTP responses with empty SD-dependent data sections instead of blocking or crashing.

#### Scenario: Dashboard file browser during SD unhealthy
- **WHEN** the dashboard StatusHandler runs while the SD card is unhealthy
- **THEN** the "files" JSON array is returned as empty and the SD directory is skipped entirely

#### Scenario: Dashboard status endpoint remains responsive
- **WHEN** the SD card is in unhealthy state
- **THEN** the /api/status endpoint returns a complete, valid JSON response including stream_samples, tilt_history, fft, and mqtt_logs (all SD-independent data)

### Requirement: Logger Skips SD Writes During Unhealthy Period
The logger SHALL skip parameter storage, failure storage, and debug log flush operations when the SD card is unhealthy, without blocking the task loop.

#### Scenario: Parameter CSV write skipped
- **WHEN** the logger processes a MONITOR_EVENT_RESULT while the SD card is unhealthy
- **THEN** the parameter CSV is formatted but not written to the SD card, and the logger continues processing other events without delay

#### Scenario: Debug log flush skipped
- **WHEN** the 1-second debug log flush timer fires while the SD card is unhealthy
- **THEN** the flush is skipped and the debug buffer retains its contents (may be flushed after cooldown if buffer not overwritten)
