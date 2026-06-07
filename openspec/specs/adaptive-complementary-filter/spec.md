# adaptive-complementary-filter Specification

## Purpose
Adaptive complementary filter for orientation estimation. Dynamically adjusts fusion weight (alpha) based on accelerometer magnitude error to bias toward gyro integration during disturbances.

## Requirements
### Requirement: Adaptive Alpha Computation
The adaptive complementary filter SHALL compute a variable fusion weight `alpha` per sample based on the accelerometer magnitude error, biasing toward gyro integration during disturbances.

#### Scenario: Normal conditions (near 1g)
- **WHEN** `|accel_magnitude - 1.0|` is approximately zero
- **THEN** `weight = 1.0 / (1.0 + k_gain * error)` SHALL be approximately 1.0
- **THEN** `alpha = 1.0 - (1.0 - alpha_base) * weight` SHALL be approximately `alpha_base` (0.98)

#### Scenario: High disturbance (large accel error)
- **WHEN** `|accel_magnitude - 1.0|` is large (e.g., > 0.5g)
- **THEN** `weight` SHALL approach zero
- **THEN** `alpha` SHALL approach 1.0 (trust gyro integration entirely)

#### Scenario: Alpha remains in valid range
- **WHEN** any valid accelerometer reading is processed
- **THEN** `alpha` SHALL always satisfy `alpha_base <= alpha <= 1.0`
- **THEN** the filter SHALL never produce NaN or inf

### Requirement: Filter State and Interface Compatibility
The adaptive complementary filter SHALL maintain the same state variables and output interface as the existing `filter::Complementary` class for drop-in replacement.

#### Scenario: Same public interface
- **WHEN** `filter::AdaptiveComplementary` replaces `filter::Complementary`
- **THEN** `update(accel, gyro, dt)` SHALL accept the same parameter types
- **THEN** `pitch()` and `roll()` SHALL return degrees as before
- **THEN** no callers SHALL need interface changes

#### Scenario: Constructor parameters
- **WHEN** `AdaptiveComplementary` is constructed
- **THEN** the constructor SHALL accept `alpha_base` (default 0.98f) and `k_gain` (default 50.0f)
- **THEN** `MonitorConfig` SHALL gain `filter_alpha_base` and `filter_k_gain` fields with defaults matching Kconfig

### Requirement: Corrected Axis Convention
The adaptive complementary filter SHALL use the corrected branch-frame axis formulas: pitch from `atan2(ax, az)` driven by `gyro[1]`, roll from `atan2(-ay, az)` driven by `gyro[0]`.

#### Scenario: Pitch computation (branch sag)
- **WHEN** accelerometer reads `(ax, ay, az)` in the branch frame (z-up, x-down, y-toward-joint)
- **THEN** accel pitch SHALL be `atan2(ax, az)` — rotation about y-axis (sag)
- **THEN** gyro pitch SHALL integrate `gyro[1]` (y-axis gyro)

#### Scenario: Roll computation (branch twist)
- **WHEN** accelerometer reads `(ax, ay, az)` in the branch frame
- **THEN** accel roll SHALL be `atan2(-ay, az)` — rotation about x-axis (twist)
- **THEN** gyro roll SHALL integrate `gyro[0]` (x-axis gyro)

### Requirement: Backport Axis Fix to Basic Complementary
The existing `filter::Complementary` class SHALL also be updated with the corrected axis formulas to maintain consistency between filter implementations.

#### Scenario: Basic complementary uses corrected formulas
- **WHEN** `filter::Complementary::update()` is called
- **THEN** accel pitch SHALL be `atan2(accel[0], accel[2])` (was `atan2(accel[1], sqrt(accel[0]²+accel[2]²))`)
- **THEN** accel roll SHALL be `atan2(-accel[1], accel[2])` (was `atan2(-accel[0], accel[2])`)
- **THEN** gyro pitch SHALL integrate `gyro[1]` (unchanged)
- **THEN** gyro roll SHALL integrate `gyro[0]` (unchanged)
