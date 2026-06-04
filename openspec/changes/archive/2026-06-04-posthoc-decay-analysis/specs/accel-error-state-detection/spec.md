# accel-error-state-detection Delta Specification

**Change:** posthoc-decay-analysis
**Base Spec:** openspec/specs/accel-error-state-detection/spec.md

## ADDED Requirements

### Requirement: Two-Threshold Accel Error Detection
The accel error state detection SHALL use only two thresholds: `K_HIGH` for IDLE→DISTURBED entry, and `K_LOW` for DISTURBED→IDLE exit (with debounce). `K_MID` SHALL be removed from Kconfig.

#### Scenario: IDLE to DISTURBED uses K_HIGH
- **WHEN** the node is in `IDLE`
- **WHEN** `accel_err_var` exceeds `max(accel_err_baseline_var × K_HIGH, K_ABS_MIN_ACCEL_VAR)`
- **THEN** the state SHALL transition to `DISTURBED`

#### Scenario: DISTURBED to IDLE uses K_LOW
- **WHEN** the node is in `DISTURBED`
- **WHEN** `accel_err_var` drops below `max(accel_err_baseline_var × K_LOW, K_ABS_MIN_ACCEL_VAR)` for `CONFIG_MONITOR_DISTURBED_EXIT_DEBOUNCE` consecutive samples
- **THEN** the state SHALL transition to `IDLE`

#### Scenario: K_MID removed
- **WHEN** the Kconfig is configured
- **THEN** `CONFIG_MONITOR_K_MID_X1000` SHALL NOT exist
- **THEN** no state transition SHALL reference a K_MID threshold


