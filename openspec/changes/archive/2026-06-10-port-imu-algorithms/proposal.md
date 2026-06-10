## Why

The Python `imu_algorithms` package now contains the validated IMU event analysis path, but firmware still mixes older roll/pitch modal analysis with a partially ported gyro/TKEO detector. Port the selected algorithms so ESP32-S3 results match the offline pipeline while keeping the node payload stable.

## What Changes

- Port the realtime gyro magnitude + TKEO disturbance detector into firmware with fixed-size buffers and the existing DISTURBED buffer refresh behavior.
- Store calibrated gyro and accelerometer event samples needed for post-event extraction.
- Replace final DISTURBED->IDLE modal extraction with the selected `imu_algorithms` flow:
  - TKEO-based decay onset detection
  - dominant signed gyro axis selection
  - FFT natural frequency using `extract_natural_frequency`
  - peak-hold envelope damping regression
  - damping confidence reporting
- Preserve 52 Hz polling; no IMU FIFO/DRDY migration.
- Do not classify events and do not publish event metadata such as event type, onset, offset, peak gyro magnitude, or duration.
- Update MQTT and SD parameter output only by adding damping confidence to the current monitoring interface.
- Ignore acoustic emission changes in this scope.
- Fix current build configuration drift where `CONFIG_MONITOR_DSP_*` values are referenced but not defined.

## Capabilities

### New Capabilities
- `imu-event-analysis`: Realtime event buffering and post-event IMU parameter extraction from calibrated gyro/accel samples.

### Modified Capabilities
- `free-decay-analysis`: Replace current roll/pitch centerline modal result path with FFT natural frequency and peak-hold envelope damping from the IMU event segment.
- `logger-fsm-adaptation`: Add damping confidence to the existing parameter payload without adding event classification or event boundary fields.
- `node-state-machine`: Align DISTURBED entry/exit requirements with the gyro magnitude + TKEO detector and keep the existing refresh transition for long disturbances.

## Impact

- `components/monitor/`: monitor result structure, detector configuration, event buffers, post-event extraction, tests.
- `components/logger/`: CSV/JSON parameter formatting and headers for confidence fields.
- `components/dashboard/`: optional display of confidence while preserving existing status endpoint shape.
- `components/monitor/Kconfig`: add missing DSP detector threshold symbols and any bounded extraction thresholds.
- `mqtt_interface.md`: document confidence field addition only.
