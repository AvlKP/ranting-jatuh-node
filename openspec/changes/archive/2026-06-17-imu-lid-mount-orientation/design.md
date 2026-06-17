## Context

The PCB carries an LSM6DS3 IMU whose silicon axes are assumed to align with the branch frame (Z-up, X-down, Y-toward-joint). This assumption holds when the PCB is mounted on the enclosure body. Mounting on the lid instead requires a 180° rotation of the PCB around the sensor's Y-axis, inverting the X and Z axes relative to the branch frame. Without correction, pitch/roll estimation and gyro integration produce incorrect results.

Current pipeline order in `Monitor::Update()` (`monitor.cpp:127-143`):
1. Read raw IMU data (sensor frame)
2. Subtract calibration biases (sensor frame)
3. Feed to adaptive complementary filter (assumes branch frame)

The transform inserts between steps 2 and 3.

## Goals / Non-Goals

**Goals:**
- Support lid-mount (180° Y-flip) and body-mount (original) PCB orientations via compile-time selection.
- Transform is transparent to all downstream code — filter, disturbance detector, modal analyzer see branch-frame data regardless of mount.
- Zero overhead when body-mount is selected (dead-code elimination).

**Non-Goals:**
- Runtime orientation switching (Kconfig only).
- Arbitrary rotation matrices or support for other mount angles.
- Auto-detection of mounting orientation.
- Recalibration workflow — biases are sensor-intrinsic and applied in sensor frame before the transform.

## Decisions

### 1. Transform placement: after bias subtraction, before filter

**Choice:** Negate X and Z on calibrated values, before constructing `accel_vec`/`gyro_vec` arrays.

**Rationale:** Biases are sensor-silicon intrinsic offsets. They must be subtracted in the sensor frame where they were measured. The rotation converts sensor frame → branch frame and must happen after bias subtraction. This means existing calibration values remain valid with no changes.

**Alternative considered:** Transform raw values before bias subtraction, then also transform the biases. Rejected — adds unnecessary coupling between calibration and mounting, and breaks if biases are re-measured in the original orientation.

### 2. Kconfig bool in monitor component

**Choice:** Add `CONFIG_MONITOR_IMU_MOUNT_LID` (bool, default `n`) to `components/monitor/Kconfig`.

**Rationale:** The monitor component owns the IMU processing pipeline. The `MONITOR_` prefix is consistent with all existing config options in this Kconfig. A bool is sufficient since only two orientations exist.

**Alternative considered:** Kconfig choice enum with named orientations. Rejected — overkill for two states, and the user confirmed no other mount variants are needed.

### 3. Compile-time conditional with `#if`

**Choice:** Use `#if CONFIG_MONITOR_IMU_MOUNT_LID` to gate the sign-flip block. When disabled, the compiler eliminates the negations entirely.

**Rationale:** Zero runtime cost for the default (body-mount) case. The branch is evaluated at compile time, not runtime. This matches the pattern used elsewhere in the codebase (e.g., `#if CONFIG_MONITOR_IMU_CALIBRATION`).

**Alternative considered:** Runtime bool set from Kconfig default. Rejected — unnecessary branch in hot path when only compile-time selection is needed.

### 4. Transform math: R_y(180°) = negate X and Z

The 180° rotation around the Y-axis produces:
```
branch_ax = -sensor_ax    branch_gx = -sensor_gx
branch_ay =  sensor_ay    branch_gy =  sensor_gy
branch_az = -sensor_az    branch_gz = -sensor_gz
```

No matrix multiply needed — two sign flips per vector. Applied to both accelerometer and gyroscope.

## Risks / Trade-offs

- **[Risk] Default biases measured in original orientation may have small errors after rotation** → Mitigation: The biases are sensor-intrinsic (zero-rate offset for gyro, near-zero for accel). They are subtracted in sensor frame before rotation, so they remain valid. If precision is ever a concern, recalibrate in the field.
- **[Risk] Forgetting to update Kconfig when hardware variant changes** → Mitigation: The Kconfig option has a descriptive help text. Build system makes it visible in `menuconfig`.
- **[Trade-off] Compile-time only means two firmware binaries for two mount variants** → Accepted: User explicitly chose this over runtime switching. Binary size and hot-path performance are prioritized.
