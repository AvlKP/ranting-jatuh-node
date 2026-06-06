## ADDED Requirements

### Requirement: Logger stores parameter payloads as proper CSV with new fields
The logger SHALL format parameter payloads as valid comma-separated values (CSV) matching the header in the SD card storage file, rather than JSON strings. The logger SHALL include the new FSM fields `natural_freq_roll_hz`, `natural_freq_pitch_hz`, and `state` in the CSV line and header.

#### Scenario: Appending parameter to SD card
- **WHEN** the logger processes a MonitorResult for SD card storage
- **THEN** it formats a CSV line containing all fields including `natural_freq_roll_hz`, `natural_freq_pitch_hz`, and `state` in the order defined by the CSV header.
- **THEN** the output is a valid comma-separated string, not a JSON object.
