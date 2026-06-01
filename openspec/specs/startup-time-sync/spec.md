# startup-time-sync Specification

## Purpose
TBD - created by archiving change add-startup-time-sync. Update Purpose after archive.
## Requirements
### Requirement: Startup Time Synchronization
The system SHALL perform an SNTP time synchronization during boot, after WiFi initialization and before any data collection or logging tasks start.

#### Scenario: Successful time sync at boot
- **WHEN** the system boots and WiFi initialization succeeds
- **THEN** the system SHALL attempt SNTP time synchronization using the configured NTP server
- **THEN** the system SHALL wait up to the configured NTP timeout for a valid time response
- **THEN** upon successful sync, the system clock SHALL be set to a valid epoch time (≥ 1672531200)
- **THEN** all subsequent log entries and data timestamps SHALL use the synchronized clock

#### Scenario: Time sync fails at boot (network unavailable)
- **WHEN** the system boots and WiFi connection fails or NTP server is unreachable
- **THEN** the system SHALL log a warning indicating time sync failure
- **THEN** the system SHALL NOT abort the boot sequence
- **THEN** the system SHALL continue startup with the unsynchronized clock
- **THEN** the periodic time sync during MQTT publish cycles SHALL eventually correct the clock

#### Scenario: Time sync at boot in dashboard mode
- **WHEN** the system boots with `CONFIG_DASHBOARD_ENABLE` active
- **THEN** the system SHALL use the already-persistent WiFi connection for time sync
- **THEN** the system SHALL NOT disconnect WiFi after sync

#### Scenario: Time sync at boot in non-dashboard mode
- **WHEN** the system boots without dashboard mode
- **THEN** the system SHALL connect WiFi, perform time sync, and disconnect WiFi after sync completes
- **THEN** the WiFi disconnect SHALL follow the same pattern used by MQTT publish paths

### Requirement: Time Sync API Accessibility
The startup time sync function SHALL be exposed as a public API in the `logger::mqtt` namespace so it can be called from the application initialization sequence.

#### Scenario: External invocation of time sync
- **WHEN** `app_main()` calls the startup time sync function after `logger::mqtt::Init()`
- **THEN** the function SHALL perform WiFi connection (if needed), SNTP sync, and WiFi cleanup
- **THEN** the function SHALL return a boolean indicating sync success

