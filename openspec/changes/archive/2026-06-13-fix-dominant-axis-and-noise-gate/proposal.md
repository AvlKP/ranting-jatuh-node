## Why

The C++ `ComputeDominantAxisSway()` selects the dominant gyro axis using cumulative sum of gyro samples, which approaches zero for symmetric oscillations (typical branch decay). Python reference uses peak-to-peak on integrated gyro angle, which correctly identifies the axis with largest oscillation amplitude. Additionally, C++ computes damping on all disturbances including noise-level flick events, producing spurious damping values that Python gates out.

## What Changes

- Fix `ComputeDominantAxisSway()` to use peak-to-peak angular displacement per axis instead of cumulative sum, matching the Python reference algorithm
- Add a minimum-energy noise gate to `AnalyzeImuEvent()`: skip damping computation when gyro magnitude signal energy is too low (preventing spurious damping on noise/flick events), without introducing event type classification

## Capabilities

### New Capabilities
- `noise-gate`: Reject damping computation on events whose gyro magnitude signal energy is below a configurable threshold, preventing noise artifacts in published damping values

### Modified Capabilities
- `imu-event-analysis`: Dominant axis selection changed from cumulative sum to peak-to-peak on integrated gyro angle; noise gate added before damping computation

## Impact

- Affected code: `components/monitor/monitor.cpp` (`ComputeDominantAxisSway`, `AnalyzeImuEvent`)
- Affected specs: `imu-event-analysis`, new `noise-gate`
- Breaking: dominant axis selection may change which axis is chosen for FFT/damping on large symmetric oscillations, potentially changing natural frequency and damping values for those events (improvement, not regression)
