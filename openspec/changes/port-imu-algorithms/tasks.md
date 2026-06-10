## 1. Configuration and Contracts

- [x] 1.1 Add missing `MONITOR_DSP_TKEO_HIGH_X10`, `MONITOR_DSP_TKEO_LOW_X10`, `MONITOR_DSP_GMAG_ONSET_X100`, and `MONITOR_DSP_GMAG_QUIET_X100` Kconfig entries with defaults matching `imu_algorithms`.
- [x] 1.2 Add `damping_confidence` to `monitor::MonitorResult` using a fixed-size representation suitable for event payload copying.
- [x] 1.3 Remove or isolate stale HPF-only FSM assumptions from monitor specs/tests affected by the gyro/TKEO detector path.
- [x] 1.4 Update `mqtt_interface.md` to document only the new `damping_confidence` field and shared dominant-axis semantics for existing numeric fields.

## 2. Event Buffers and Detector

- [x] 2.1 Add fixed-capacity event sample storage for calibrated `gx`, `gy`, `gz`, `ax`, `ay`, `az`, and `gmag`.
- [x] 2.2 Add matching short pre-trigger storage so IDLE->DISTURBED copies recent calibrated samples into the event buffer.
- [x] 2.3 Update `Monitor::Update()` and `PushSample()` data flow so calibrated gyro/accel samples reach event storage without heap allocation.
- [x] 2.4 Align `DspDisturbanceDetector` with `imu_algorithms.EventDetector` quiet re-entry semantics and Kconfig thresholds.
- [x] 2.5 Preserve current DISTURBED refresh transition and reset event buffers with recent short-buffer samples after refresh.

## 3. Post-Event Extraction

- [x] 3.1 Port TKEO-based decay onset detection over calibrated gyro magnitude with quality result gates.
- [x] 3.2 Port active-region detection and integrate calibrated gyro axes to compute per-axis sway.
- [x] 3.3 Select the dominant signed gyro axis from largest absolute integrated sway.
- [x] 3.4 Port FFT natural frequency extraction using mean removal, Hann window, ESP-DSP FFT, and configured modal frequency bounds.
- [x] 3.5 Port peak-hold envelope generation over gyro magnitude.
- [x] 3.6 Port bounded log-envelope damping regression with `"high"`, `"medium"`, and `"low"` confidence.
- [x] 3.7 Map dominant-axis natural frequency and damping into existing `MonitorResult` numeric fields without adding event metadata fields.

## 4. Logger and Dashboard Consumers

- [x] 4.1 Update logger CSV header and CSV formatting to append `damping_confidence`.
- [x] 4.2 Update logger JSON formatting to append `damping_confidence` and keep event metadata absent.
- [x] 4.3 Update logger formatting tests to check confidence presence and absence of event classification/boundary fields.
- [x] 4.4 Update dashboard status endpoint and UI only as needed to expose confidence while preserving current field names.

## 5. Tests and Verification

- [x] 5.1 Add unit tests for detector entry, quiet debounce, and renewed-disturbance debounce reset using gyro/TKEO thresholds.
- [x] 5.2 Add unit tests for fixed event buffer wrap/refresh behavior and bounded sample counts.
- [x] 5.3 Add unit tests for decay onset, dominant-axis selection, FFT frequency extraction, envelope damping, and confidence gates.
- [x] 5.4 Add replay-style test using representative `imu_algorithms` sample data where feasible.
- [x] 5.5 Run `idf.py build` and confirm the current missing `CONFIG_MONITOR_DSP_*` build failure is resolved.
- [x] 5.6 Run monitor Unity test build for `TEST_COMPONENTS=monitor`.
- [x] 5.7 Run `idf.py size` and record RAM/flash impact of event buffers and extraction code.
