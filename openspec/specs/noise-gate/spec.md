# noise-gate Specification

## Purpose
TBD - created by archiving change fix-dominant-axis-and-noise-gate. Update Purpose after archive.
## Requirements
### Requirement: Noise Gate for Damping Computation
The monitor SHALL skip damping ratio computation when the stored gyro magnitude segment has insufficient signal energy relative to baseline noise, preventing spurious damping values on noise-level events. The gate SHALL NOT classify events into semantic types.

#### Scenario: Signal energy below noise threshold
- **WHEN** post-hoc IMU event analysis runs on DISTURBED to IDLE transition
- **WHEN** the peak gyro magnitude in the event segment is below the configured noise-gate threshold
- **THEN** damping ratio SHALL be set to 0.0
- **THEN** damping confidence SHALL be set to "low"
- **THEN** natural frequency SHALL still be computed and published normally
- **THEN** no event type classification SHALL be performed

#### Scenario: Signal energy above noise threshold
- **WHEN** post-hoc IMU event analysis runs on DISTURBED to IDLE transition
- **WHEN** the peak gyro magnitude in the event segment meets or exceeds the configured noise-gate threshold
- **THEN** damping computation SHALL proceed through the normal decay-onset, frequency, and envelope fitting pipeline
- **THEN** no event type classification SHALL be performed

#### Scenario: Noise-gate threshold is configurable
- **WHEN** firmware is built with default Kconfig settings
- **THEN** the noise-gate peak gyro magnitude threshold SHALL default to 8.0 dps
- **THEN** the threshold SHALL be configurable via `CONFIG_MONITOR_NOISE_GATE_GMAG_X10` (default 80, representing 8.0 dps)

