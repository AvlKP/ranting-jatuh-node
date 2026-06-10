## Context

`imu_algorithms` contains a two-plane IMU analysis pipeline: O(1) realtime detection from calibrated gyro magnitude and TKEO, followed by post-event parameter extraction over a bounded event segment. Current firmware already has a partial gyro/TKEO detector, roll/pitch/gmag history buffers, ESP-DSP FFT support, logger/dashboard consumers, and MQTT/SD parameter formatting.

The current repository has configuration drift: `MonitorConfig` references `CONFIG_MONITOR_DSP_TKEO_HIGH_X10`, `CONFIG_MONITOR_DSP_TKEO_LOW_X10`, `CONFIG_MONITOR_DSP_GMAG_ONSET_X100`, and `CONFIG_MONITOR_DSP_GMAG_QUIET_X100`, but `components/monitor/Kconfig` does not define them. `idf.py build` fails until these settings are restored.

Hardware constraints for this change are fixed: 52 Hz IMU polling, no FIFO/DRDY migration, ESP32-S3 target, gyroscope range is sufficient, max event duration fits a 2048-sample event ring, and acoustic emission is out of scope.

## Goals / Non-Goals

**Goals:**
- Port the selected `imu_algorithms` firmware path using deterministic fixed-capacity C++.
- Keep 52 Hz polling and the existing DISTURBED refresh transition.
- Use `extract_natural_frequency` semantics: detrend, Hann window, FFT, bounded frequency search.
- Compute damping from a peak-hold envelope and report confidence.
- Add only damping confidence to the current MQTT/SD parameter interface.
- Preserve existing numeric parameter fields for current consumers.
- Fix missing DSP detector Kconfig symbols so the project builds.

**Non-Goals:**
- No event classification.
- No MQTT/SD fields for event type, onset, offset, peak gyro magnitude, duration, dominant axis, or raw event metadata.
- No IMU FIFO/DRDY rewrite.
- No acoustic emission changes.
- No deep-sleep or power-state redesign.
- No fixed-point rewrite in this change.

## Decisions

### D1: Two-plane firmware architecture

Realtime plane runs inside the monitor update path:

```text
read IMU
  -> subtract calibration bias
  -> compute gmag
  -> update 3-sample TKEO window
  -> update Schmitt detector
  -> write fixed event buffers
```

Post-event plane runs only on final `DISTURBED->IDLE`:

```text
event segment
  -> decay onset from non-negative TKEO burst
  -> active region from gmag threshold
  -> integrate calibrated gyro axes for sway
  -> select dominant signed gyro axis by largest sway
  -> FFT natural frequency on decay segment
  -> peak-hold envelope damping
  -> publish current fields + damping_confidence
```

Rationale: detection stays O(1) per sample and extraction cost is paid only after an event. This matches `imu_algorithms` while keeping the monitor task bounded.

Alternatives considered: continuous FFT every sample, rejected because it wastes cycles and power; full Python output parity, rejected because user explicitly limited MQTT fields.

### D2: Fixed event sample storage

Store calibrated `gx`, `gy`, `gz`, `ax`, `ay`, `az`, and `gmag` in fixed-capacity event buffers sized for the maximum event window. Keep short pre-trigger buffers so transition into DISTURBED preserves samples immediately before onset. Reuse the existing refresh behavior when the event buffer nears capacity.

Rationale: post-event extraction needs signed gyro and accel segments; current monitor only stores roll, pitch, and gmag. Fixed arrays avoid heap allocation and keep behavior deterministic.

Alternative considered: keep only gmag plus roll/pitch. Rejected because signed-axis FFT, 3-axis sway, and accel tilt require calibrated axis samples.

### D3: Detector semantics

Use the `imu_algorithms` Schmitt detector:
- enter when `tkeo > high_threshold` or `gmag > onset_threshold`
- begin quiet counting when `tkeo < low_threshold` and `gmag < quiet_threshold`
- reset quiet counting on renewed excitation
- exit after configured quiet sample count

Rationale: this is the validated realtime detector and matches existing partial firmware code. Kconfig shall define scaled integer thresholds for reproducible builds.

Alternative considered: return to Chebyshev HPF state transition. Rejected because this change is explicitly for the new gyro/TKEO IMU algorithm path.

### D4: FFT natural frequency

Use firmware equivalent of `extract_natural_frequency`: subtract mean from the signed dominant-axis decay segment, apply a Hann window, run real FFT, and select the largest bin inside the configured modal band.

ESP-DSP `dsps_fft2r_fc32` remains the FFT engine. For non-power-of-two event lengths, copy the segment into the existing power-of-two FFT scratch buffer with zero padding to the next supported size bounded by existing scratch capacity.

Rationale: user explicitly selected FFT, and ESP32-S3 has single-precision FPU support suitable for this float path. ESP-DSP is already a project dependency.

Alternative considered: zero-crossing frequency. Rejected by user instruction.

### D5: Current payload compatibility

The extracted event produces one dominant-axis natural frequency and one damping ratio. To preserve the current MQTT/SD numeric schema:
- `natural_freq_hz` SHALL contain the dominant-axis FFT result.
- `natural_freq_roll_hz` and `natural_freq_pitch_hz` SHALL mirror `natural_freq_hz` for compatibility.
- `roll_damping_ratio` and `pitch_damping_ratio` SHALL mirror the same damping ratio for compatibility.
- `damping_confidence` SHALL be the only new payload field.

Rationale: the existing interface is roll/pitch oriented, but the selected algorithm is event/dominant-axis oriented. Mirroring prevents current consumers from seeing missing values while avoiding a broader payload redesign.

Alternative considered: leave roll/pitch fields as zero. Rejected because dashboard and downstream consumers would hide the new result.

### D6: No event classification

Do not port `classify_event()` or publish event metadata. Event type, onset/offset, peak gmag, and duration may exist internally for extraction if needed, but they SHALL NOT be exposed through MQTT/SD/dashboard payloads in this change.

Rationale: user explicitly removed classification and event metadata from scope.

## Risks / Trade-offs

- Payload mirroring can be misread as true roll/pitch-specific results -> Document that these fields carry the shared dominant-axis result until a future interface revision.
- Event arrays increase RAM use -> Use bounded arrays, prefer reusing existing history/scratch where practical, and validate image size with `idf.py size`.
- FFT zero padding differs from Python exact-length FFT -> Keep frequency band tests with synthetic known-frequency signals and document the firmware approximation.
- Build drift may hide more stale HPF/centerline assumptions -> Update specs and tests around gyro/TKEO detector behavior first.
- Confidence as a string increases line length -> Check fixed 512-byte logger line buffer tests.

