## ADDED Requirements

### Requirement: Free-fall failure publish cooldown
The system SHALL suppress repeated `FailureEvent::FreeFall` publishes within a configurable cooldown window after each successful publish.

#### Scenario: First free-fall publishes immediately
- **WHEN** the LSM6DS3 free-fall hardware flag is set AND no prior free-fall has been published since boot or reset
- **THEN** the system SHALL publish the failure event immediately

#### Scenario: Free-fall within cooldown is suppressed
- **WHEN** the LSM6DS3 free-fall hardware flag is set AND the elapsed time since the last free-fall publish is less than the configured cooldown duration
- **THEN** the system SHALL NOT publish a new free-fall failure event

#### Scenario: Free-fall after cooldown publishes again
- **WHEN** the LSM6DS3 free-fall hardware flag is set AND the elapsed time since the last free-fall publish is greater than or equal to the configured cooldown duration
- **THEN** the system SHALL publish a new free-fall failure event AND SHALL record the current timestamp as the new last-publish time

#### Scenario: Other failure types unaffected by free-fall cooldown
- **WHEN** an `AcousticEmission` failure event is detected
- **THEN** the system SHALL publish the acoustic emission failure event regardless of the free-fall cooldown state

### Requirement: Configurable cooldown duration
The free-fall cooldown duration SHALL be configurable via Kconfig with a default value of 10,000 milliseconds and accessible at runtime through `MonitorConfig`.

#### Scenario: Default cooldown value
- **WHEN** no Kconfig override is set
- **THEN** the free-fall cooldown duration SHALL default to 10,000 milliseconds

#### Scenario: Custom cooldown via Kconfig
- **WHEN** `CONFIG_MONITOR_FREEFALL_DEBOUNCE_MS` is set to a non-default value in sdkconfig
- **THEN** the runtime `MonitorConfig::freefall_debounce_ms` SHALL reflect that value
