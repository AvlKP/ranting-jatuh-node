## MODIFIED Requirements

### Requirement: CSV Debug Log Formatting
The system SHALL format the raw acceleration and tilt data as comma-separated values and buffer them in memory before writing to the CSV file. Each row SHALL include the current node state as an integer column (0=IDLE, 1=DISTURBED). Stream sample data SHALL be delivered from the monitor to the logger via a lock-free ring buffer, NOT through the esp_event system.

#### Scenario: Buffering data row
- **WHEN** new acceleration and tilt data is available
- **THEN** the system formats a row containing timestamp, accel_x, accel_y, accel_z, tilt_x, tilt_y, tilt_z, state and appends it to an in-memory ring buffer without writing to the CSV file immediately

#### Scenario: Monitor writes stream samples to ring buffer
- **WHEN** the monitor acquires a new IMU sample
- **THEN** the monitor writes a StreamSample entry to the dedicated debug ring buffer using atomic indices, without posting any esp_event

#### Scenario: Logger polls ring buffer
- **WHEN** the logger task loop performs its 1-second debug flush check
- **THEN** the logger reads all accumulated StreamSample entries from the ring buffer, formats them as CSV rows, and appends them to the storage ring buffer for batched write

### Requirement: State Column in StreamSample
The system SHALL include the current node state in each stream sample stored in the debug ring buffer.

#### Scenario: Monitor includes state
- **WHEN** the monitor writes a StreamSample to the debug ring buffer
- **THEN** the sample includes a state field reflecting the current node state (IDLE or DISTURBED) as an integer value

## REMOVED Requirements

### Requirement: Stream Sample Event Posting
**Reason**: The `esp_event_post(MONITOR_EVENT_STREAM_SAMPLE)` call at IMU rate floods the event loop queue, causing silent drops of all event types including MONITOR_EVENT_RESULT.

**Migration**: The monitor no longer posts MONITOR_EVENT_STREAM_SAMPLE events. The logger no longer registers an event handler for this event type. Stream sample data is delivered via a dedicated atomic ring buffer polled by the logger task loop.
