# chebyshev-hpf-disturbance Specification

## Purpose
Chebyshev Type 1 high-pass filter for per-axis accelerometer disturbance detection. Replaces the scalar `|accel_mag-1.0|` metric with a 2nd-order Direct Form II biquad HPF (fc=0.1 Hz) per axis, whose magnitude drives the IDLE/DISTURBED state machine.

## Requirements
### Requirement: Chebyshev HPF Per-Axis Filtering
The monitor SHALL apply a 2nd-order Chebyshev Type 1 high-pass filter to each accelerometer axis independently using Direct Form II biquad structure, with coefficients designed for fc=0.1 Hz at the configured sample rate.

#### Scenario: Biquad state initialization
- **WHEN** the monitor starts
- **THEN** all 6 biquad state variables (w1, w2 per axis) SHALL be initialized to zero
- **THEN** filter coefficients (b0, b1, b2, a1, a2) SHALL be compile-time constants

#### Scenario: Per-sample biquad update
- **WHEN** a new calibrated accelerometer sample (ax, ay, az) arrives
- **THEN** each axis SHALL be processed through its own Direct Form II biquad:
  - `w0 = x - a1*w1 - a2*w2`
  - `y = b0*w0 + b1*w1 + b2*w2`
  - `w2 = w1; w1 = w0`
- **THEN** the output SHALL be `(hpf_x, hpf_y, hpf_z)`

#### Scenario: HPF magnitude computation
- **WHEN** HPF outputs for all three axes are available
- **THEN** the monitor SHALL compute `hpf_magnitude = sqrt(hpf_x² + hpf_y² + hpf_z²)`
- **THEN** `hpf_magnitude` SHALL be the input to the disturbance FSM

### Requirement: HPF Settle Period at Startup
The monitor SHALL suppress disturbance detection for the first N samples after startup to allow the Chebyshev HPF transient to decay.

#### Scenario: Settle phase active
- **WHEN** the monitor is in its first `CONFIG_MONITOR_HPF_SETTLE_SAMPLES` samples
- **THEN** the HPF biquads SHALL still process samples (to converge state variables)
- **THEN** but state transitions SHALL NOT be evaluated
- **THEN** the node SHALL remain in IDLE regardless of HPF magnitude

#### Scenario: Settle phase complete
- **WHEN** `sample_count` reaches `CONFIG_MONITOR_HPF_SETTLE_SAMPLES`
- **THEN** state transition evaluation SHALL begin
- **THEN** the settle phase SHALL reuse the existing `tare_settle_accumulated_` counter when taring is enabled, or a dedicated counter when taring is disabled

### Requirement: HPF Threshold Detection
The monitor SHALL transition from IDLE to DISTURBED when the HPF magnitude exceeds a fixed threshold, and from DISTURBED to IDLE when it falls below the threshold for a debounce period.

#### Scenario: IDLE to DISTURBED on HPF exceedance
- **WHEN** the node is in IDLE state
- **WHEN** the HPF settle period is complete
- **WHEN** `hpf_magnitude > CONFIG_MONITOR_HPF_THRESHOLD_X1000 / 1000.0`
- **THEN** the state SHALL transition to DISTURBED immediately (no entry debounce)

#### Scenario: DISTURBED to IDLE with exit debounce
- **WHEN** the node is in DISTURBED state
- **WHEN** `hpf_magnitude < CONFIG_MONITOR_HPF_THRESHOLD_X1000 / 1000.0` for `CONFIG_MONITOR_DISTURBED_EXIT_DEBOUNCE` consecutive samples
- **THEN** the state SHALL transition to IDLE
- **THEN** post-hoc modal analysis SHALL be triggered

#### Scenario: Debounce reset on re-exceedance
- **WHEN** the node is in DISTURBED state
- **WHEN** `hpf_magnitude` drops below threshold for N < debounce samples
- **WHEN** `hpf_magnitude` then exceeds threshold again
- **THEN** the debounce counter SHALL reset to zero
- **THEN** the node SHALL remain in DISTURBED

### Requirement: HPF Configuration via Kconfig
The Chebyshev HPF parameters SHALL be configurable via Kconfig with appropriate scaling for integer-only Kconfig.

#### Scenario: Kconfig entries exist
- **WHEN** the Kconfig is processed
- **THEN** `CONFIG_MONITOR_HPF_THRESHOLD_X1000` SHALL exist with default 20 (0.020 g)
- **THEN** `CONFIG_MONITOR_HPF_SETTLE_SAMPLES` SHALL exist with default 500
- **THEN** `CONFIG_MONITOR_DISTURBED_EXIT_DEBOUNCE` SHALL remain unchanged at default 64

#### Scenario: Threshold applied at runtime
- **WHEN** the monitor evaluates state transitions
- **THEN** the threshold SHALL be computed as `CONFIG_MONITOR_HPF_THRESHOLD_X1000 / 1000.0f`
- **THEN** this single value SHALL be used for both entry and exit comparisons
