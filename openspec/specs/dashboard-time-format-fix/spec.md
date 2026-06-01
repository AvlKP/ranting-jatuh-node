# dashboard-time-format-fix Specification

## Purpose
TBD - created by archiving change fix-dashboard-time-format. Update Purpose after archive.
## Requirements
### Requirement: Dashboard Epoch Time Serialization
The dashboard system SHALL serialize sensor stream sample timestamps as absolute epoch timestamps in microseconds when the system clock is valid.

#### Scenario: Synced clock serialization
- **WHEN** the dashboard handles a `/api/status` HTTP request
- **WHEN** the current system time is valid (epoch ≥ 1672531200)
- **THEN** each sample's `ts` field in the response JSON SHALL represent the reconstructed absolute epoch time in microseconds
- **THEN** the formatted stream time in the dashboard browser table SHALL match the actual real-world wall clock time

#### Scenario: Unsynced clock serialization
- **WHEN** the dashboard handles a `/api/status` HTTP request
- **WHEN** the current system time is not yet valid (epoch < 1672531200)
- **THEN** each sample's `ts` field in the response JSON SHALL represent the raw monotonic uptime in microseconds
- **THEN** the formatted stream time in the dashboard browser table SHALL fall back to uptime relative to epoch (starting at 07:00:xx AM/08:00:xx AM)

