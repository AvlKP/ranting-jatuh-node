## ADDED Requirements

### Requirement: Calibration verification SHALL prove NVS order without repeated writes
Calibration startup verification SHALL show NVS initialization before calibration access and SHALL avoid unnecessary repeated calibration writes during normal boot.

#### Scenario: Calibration read after NVS init
- **WHEN** firmware boots with IMU calibration enabled
- **THEN** logs SHALL show NVS initialization before `calib` namespace open or `imu_bias` read
- **AND** monitor initialization SHALL not depend on logger or network initialization for NVS readiness

#### Scenario: Startup applies existing calibration
- **WHEN** calibration biases already exist in NVS
- **THEN** normal startup SHALL read and apply the stored biases
- **AND** it SHALL NOT rewrite and commit the same hard-coded biases on every boot unless an explicit calibration-update path requested it
