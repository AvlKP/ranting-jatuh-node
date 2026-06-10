# Ranting Jatuh Node Architecture

## 1. Data Processing Pipeline And Reasoning

Ranting Jatuh Node is an ESP32-S3 firmware for branch structural monitoring. It reads IMU motion and acoustic emission signals, extracts branch-state parameters, logs local data to microSD, and publishes warnings/results to MQTT.

A Python reference implementation (`imu_algorithms/`) mirrors the core algorithms for offline validation on recorded CSV data. The C++ firmware ports these algorithms to the embedded target with additional hardware-specific concerns.

### 1.1 System Pipeline

1. Boot initializes default ESP event loop, I2C bus, LSM6DS3, SD card, monitor, logger, optional dashboard, and NTP time sync.
2. Monitor task samples LSM6DS3 accel/gyro at `CONFIG_MONITOR_IMU_RATE_HZ`.
3. Raw IMU values receive static bias correction (per-axis subtraction; biases persisted in NVS).
4. Calibrated accelerometer and gyroscope feed adaptive complementary filter for roll/pitch estimation.
5. Startup tare subtracts baseline roll/pitch offset after filter settle.
6. **Gyro magnitude** `gmag = sqrt(gx^2 + gy^2 + gz^2)` is computed from calibrated gyro.
7. **Teager-Kaiser Energy Operator (TKEO)** is applied to gmag: `psi[n] = x[n-1]^2 - x[n-2]*x[n]`, using a 3-sample sliding window with 1-sample latency.
8. TKEO and gmag drive a **Schmitt-trigger state machine** (`DspDisturbanceDetector`) with hysteresis:
   - `IDLE -> DISTURBED`: `psi > tkEO_high` OR `gmag > gmag_onset`
   - `DISTURBED -> IDLE`: debounced exit requiring `N` consecutive quiet samples where `psi < tkEO_low` AND `gmag < gmag_quiet`
   - Re-entry during quiet-count mode resets the counter; pause-on-boundary behavior prevents chatter.
9. On `IDLE -> DISTURBED`, the disturbance buffer records roll, pitch, gmag, gyro axes, and accel axes from a pre-onset short buffer plus ongoing samples.
10. On `DISTURBED -> IDLE`, post-hoc modal analysis runs (see §1.3). On intermediate DISTURBED buffer refresh, sway statistics are published without modal analysis.
11. Monitor posts `MonitorResult` and `FailureResult` through `MONITOR_EVENT_BASE`. Logger receives events via ESP event handlers, queues them, writes CSV to SD, and publishes MQTT.
12. During `IDLE`, the system periodically publishes 5-minute roll/pitch mean and variance (sway, frequency, and damping fields zeroed).

### 1.2 Adaptive Complementary Filter

Estimates orientation while handling disturbed motion. The filter self-tunes its fusion weight `alpha`:

```
accel_err = |sqrt(ax^2 + ay^2 + az^2) - 1.0|
weight    = 1.0 / (1.0 + K_gain * accel_err)
alpha     = 1.0 - (1.0 - alpha_base) * weight
pitch     = alpha * (pitch + gy * dt) + (1-alpha) * atan2(ax, az)
roll      = alpha * (roll  + gx * dt) + (1-alpha) * atan2(-ay, az)
```

Near 1 g, accelerometer tilt is trusted more (weight ~1, alpha ~alpha_base). During strong dynamic acceleration, `|accel_magnitude - 1.0|` rises, weight approaches 0, alpha approaches 1.0, and gyro integration dominates.

### 1.3 Post-Hoc Modal Analysis (DISTURBED -> IDLE)

Runs when the Schmitt trigger exits to IDLE. The C++ pipeline (`AnalyzeImuEvent`) matches the Python reference pipeline:

1. **TKEO Energy-Burst Decay Onset Detection** (`FindDecayOnsetTkeo`):
   - Compute non-negative TKEO `psi_pos[n] = max(psi[n], 0)` over the buffered gmag history.
   - `energy_floor = (10 * baseline_gmag)^2` where `baseline_gmag = 0.35 dps`.
   - `threshold = max(energy_floor, 0.30 * max(psi_pos))` — selects last energy burst.
   - Find last index where `psi_pos > threshold`, then snap to nearest local gmag peak within ±0.45 s.
   - Validate: minimum 20 decay samples, minimum 2× amplitude drop from onset to tail.
   - Returns decay onset index and quality (Reliable/Low/None).

2. **Dominant Axis Selection** (`ComputeDominantAxisSway`):
   - Integrate each calibrated gyro axis over the disturbance window: `angle_axis = sum(gyro_axis * dt)`.
   - Dominant axis = axis with largest absolute cumulative angle.

3. **Natural Frequency Estimation** (`ComputeSignedAxisNaturalFrequency`):
   - Extract decay segment of the dominant signed gyro axis (not gmag).
   - De-mean, apply Hann window, zero-pad short segments to 512/1024 bins.
   - FFT via ESP-DSP; peak bin in configurable band (default 0.5–25 Hz).
   - For long records, Welch-style overlapping 1024-sample segments are averaged.

4. **Peak-Hold Envelope** (`ComputePeakHoldDamping`):
   - Asymmetric peak detector on gmag over the decay region:
     ```
     env[0] = max(0, gmag[0])
     env[n] = max(max(0, gmag[n]), alpha * env[n-1])
     alpha   = exp(-2*pi * fc * dt),    fc = 2 Hz
     ```
   - Rises instantly with signal peaks, decays exponentially between peaks.

5. **Bounded OLS Damping Ratio**:
   - Skip first ~1 cycle of the envelope (filter transient).
   - Compute lower fit bound: `max(4 * baseline_gmag, 0.03 * peak_env, 1e-6)`.
   - Fit only samples above the lower bound.
   - Ordinary least squares on `ln(envelope)` vs time:
     ```
     ln(a(t)) = ln(A0) - zeta * omega_n * t
     slope     = -zeta * omega_n
     zeta      = -slope / (2*pi * fn)
     ```
   - Confidence classification:
     - **High**: R² > 0.90, ≥3 cycles, ≥2× drop, ≥4 samples/cycle, quality=Reliable
     - **Medium**: R² > 0.70, ≥2× drop
     - **Low**: otherwise

6. **Auxiliary Parameters**:
   - **Sway statistics**: max and mean peak-to-peak amplitude on roll/pitch tilt histories (separate from dominant gyro axis sway).
   - **IDLE baseline**: 5-minute rolling mean and variance of roll/pitch during quiet periods.

### 1.4 Failure Detection

Two parallel hardware-driven paths, independent of the main processing pipeline:

- **Free-fall**: LSM6DS3 internal motion-detection interrupt signals free-fall; published as `free_fall` failure.
- **Acoustic Emission**: GPIO rising-edge ISR (counts events) or ADC threshold comparison per sample; published as `acoustic_emission` failure. The GPIO path assumes AE frontend output holds/pulses long enough for ISR capture. Final intended AE path is ADC plus DSP.

### 1.5 MQTT Payload Model

- Parameters publish to `ranting/{node_id}/parameters` (JSON, content-type `application/json`).
- Failures publish to `ranting/{node_id}/failures` (CSV, content-type `text/csv`).
- Verification publishes to `ranting/{node_id}/verify`.
- Deployment intent: batch parameter publishes to save power.
- Debug intent: publish per event for faster feedback.

### 1.6 Python Reference Implementation

`imu_algorithms/` provides an offline, Python implementation of the core algorithms for validation, tuning, and regression testing against recorded CSV data. Key modules:

| Module | Purpose |
|--------|---------|
| `_calibration.py` | Static bias subtraction (hardcoded sensor constants) |
| `_detection.py` | `tkeo()`, `tkeo_streaming()`, `EventDetector` (Schmitt trigger), `classify_event()` |
| `_envelope.py` | `envelope_peak_hold()`, `find_decay_onset_tkeo()`, `damping_from_envelope()` |
| `_extraction.py` | `extract_natural_frequency()` (FFT), `extract_frequency_zc()`, `extract_frequency_pk()`, `extract_active_region()`, `extract_active_sway()`, `extract_tilt()`, `Pipeline` class |
| `_ringbuffer.py` | Ring buffer for real-time streaming path |
| `_io.py` | CSV loading utilities |

**Algorithm parity**: The core algorithms (TKEO, Schmitt trigger, decay onset, peak-hold envelope, OLS damping, FFT natural frequency) are functionally identical between Python and C++. Notable differences:

| Feature | Python | C++ |
|---------|--------|-----|
| Event classification | `classify_event()`: pull_release, oscillation, flick, pull_hold | Not implemented |
| Dynamic gating | `is_dynamic()` skips flick/short events | All disturbances analyzed |
| Dominant axis sway | Peak-to-peak on integrated gyro | Cumulative sum (known limitation, see §1.7) |
| Frequency validation | FFT + zero-crossing + peak-to-peak, cross-checked | FFT only |
| Active region extraction | RMS-based threshold subsegment | Not implemented |
| Static tilt | atan2 on accelerometer segment means | Adaptive complementary filter (dynamic orientation) |
| Orientation filter | None | Adaptive complementary + tare subtraction |
| Free-fall / AE | Not simulated | Hardware-interrupt driven |
| Calibration | Hardcoded sensor constants | NVS-persisted, runtime updatable |

### 1.7 Known Limitations

1. **Dominant axis selection** (§1.3, step 2): Uses cumulative sum of gyro (`sum(gyro_axis * dt)`), which approaches zero for symmetric oscillations. Python correctly uses peak-to-peak amplitude (`max(cumsum) - min(cumsum)`). Fix pending.

2. **No frequency cross-validation**: Python compares FFT, zero-crossing, and peak-to-peak frequency estimates, warning on >20% mismatch. C++ relies on FFT alone, with no second-opinion mechanism for signal-quality issues.

3. **No event-type gating**: Python skips damping computation for short/low-energy events (flick, pull_hold) via `is_dynamic()`. C++ computes damping on all disturbances, which may produce noisy/unreliable values for non-oscillatory events.

4. **FFT tail truncation**: For disturbances exceeding 1024 decay samples, only the last 1024 are analyzed. Early decay may have higher SNR than the tail portion.

## 2. ESP-IDF Specific Implementation Choices

ESP-IDF v5.5.4 is used because the target is ESP32-S3 and firmware needs native access to I2C, SDMMC/SDSPI, ADC, GPIO ISR, FreeRTOS, WiFi, MQTT, NVS, HTTP server, and ESP-DSP FFT.

FreeRTOS is used because sensing, logging, network, and dashboard workloads have different timing requirements. Monitor task is pinned to core 1, priority 5, 8192-byte stack. Logger task is pinned to core 0, priority 4, 6144-byte stack. Reason: periodic sensor processing stays isolated from SD/MQTT latency.

`vTaskDelayUntil` schedules monitor sampling. Reason: fixed-period loop is simple, deterministic enough for current IMU rates, and easier to debug than timer ISR based processing. IMU FIFO is intentionally not used yet; it remains a future power/performance optimization.

ESP event loop decouples monitor from logger/dashboard. Monitor posts `MONITOR_EVENT_RESULT` and `MONITOR_EVENT_FAILURE`. Logger/dashboard register handlers. Reason: monitor does not directly depend on storage, MQTT, or HTTP implementation details.

FreeRTOS queue inside logger absorbs event bursts. Event handler copies payload into queue with zero wait and tracks drops. Reason: ESP event callback stays short; blocking file and network work runs only in logger task.

I2C uses ESP-IDF bus-device master API at 400 kHz. LSM6DS3 driver receives read/write callbacks rather than owning ESP-IDF bus handles. Reason: driver stays testable and hardware transport stays in app layer.

SD storage uses ESP VFS FAT over SDMMC 1-bit by default, with SDSPI option in normal app. Reason: FAT CSV files are easy to inspect on PC and SDMMC 1-bit matches custom PCB wiring.

MQTT uses ESP-MQTT v5 with content type properties. WiFi is initialized once, then connected only for sync/publish when dashboard is disabled. When dashboard is enabled, WiFi stays persistent for HTTP access. Reason: deployment saves battery; development dashboard favors observability.

NVS stores node identity and calibration data. `CONFIG_LOGGER_NODE_ID` can override NVS for factory provisioning. Empty node ID generates an adjective-noun ID using `esp_random()` and persists it in NVS.

Dashboard uses ESP HTTP server on port `CONFIG_DASHBOARD_PORT`. `/api/status` streams JSON chunks containing WiFi/MQTT status, heap, node ID, current state, latest samples, SD files, tilt history, FFT data, and MQTT logs. Reason: debug UI avoids external debugger dependency during field trials.

ESP-DSP provides FFT implementation. Reason: firmware gets optimized DSP routines on ESP32-S3 without custom FFT code.

Memory strategy uses fixed-size `std::array` buffers for histories, FFT workspace, PSD, extrema lists, stream samples, MQTT logs, CSV lines, and publish batch. Reason: C++ exceptions are disabled and real-time behavior should avoid heap churn in hot paths.

Raw logger build mode (`APP_BUILD_RAW_LOGGER`) swaps normal monitor app for direct IMU-to-SD CSV recorder. Reason: collect raw data for algorithm validation and calibration without changing production pipeline.

Startup verification can check SD write/read, MQTT publish, monitor output, and stack high watermark. Reason: catch board/config failures early before unattended monitoring.

## 3. Component Inventory

### 3.1 Active Components

| Component | Directory | Purpose |
|-----------|-----------|---------|
| `monitor` | `components/monitor/` | IMU sampling, signal conditioning, disturbance detection, modal analysis |
| `logger` | `components/logger/` | SD CSV logging, MQTT publishing, event queuing |
| `lsm6ds3` | `components/lsm6ds3/` | LSM6DS3TR driver (I2C register read/write) |
| `filter` | `components/filter/` | General-purpose orientation filter library. Only `adaptive_complementary_filter.hpp` is active in this project; other variants (Madgwick, Kalman, EKF, Complementary) are retained as library code. |
| `dashboard` | `components/dashboard/` | HTTP server for debug observability |

### 3.2 Unused Library Variants

The `components/filter/` directory is a general-purpose orientation filter library. The following variants exist in the library but are not instantiated in this project:

| Filter | File | Reason Unused |
|--------|------|---------------|
| `MadgwickFilter` | `madgwick_filter.hpp` | Orientation uses adaptive complementary filter |
| `KalmanFilter` | `kalman_filter.hpp` | Never integrated |
| `EkfFilter` | `ekf_filter.cpp`, `ekf_filter.hpp` | Compiled but never instantiated |
| `ComplementaryFilter` | `complementary_filter.hpp` | Superseded by adaptive variant |

### 3.3 Removed Legacy Artifacts

The following were removed in the `remove-dead-code` change (2026-06). These were design artifacts from earlier iterations that accumulated zero production call sites:

| Category | Removed |
|----------|---------|
| Disturbance detection | `ChebyshevHpf` class (`chebyshev_hpf.hpp`). Designed for HPF-based detection; never integrated into `Monitor::Update()`. Superseded by TKEO-based `DspDisturbanceDetector`. |
| Old modal analysis (9 methods) | `FindDecayRegion`, `AnalyzeModalAxis`, `DetectRawExtrema`, `CollapseExtremaLobes`, `BuildCenterlinePairs`, `SubtractCenterline`, `SelectPairEnvelope`, `ComputeResidualNaturalFrequency`, `ComputeDampingRegression`. Centerline-based per-axis modal analysis. Superseded by `AnalyzeImuEvent`. |
| Old modal analysis (8 types) | `DecayRegion`, `ExtremaKind`, `ExtremaPoint`, `ExtremaList`, `CenterlinePair`, `CenterlinePairList`, `PeakList`, `ModalAxisResult`. |
| Dead frequency methods | `ComputeNaturalFrequency` (declared, never defined), `ComputeAxisNaturalFrequency`, `ComputeGmagNaturalFrequency`. Superseded by `ComputeSignedAxisNaturalFrequency`. |
| Debug dump | `DumpDebugToSD` method and `CONFIG_MONITOR_DEBUG_DUMP` preprocessor block. Read from scratch buffers never written by active code. |
| Vestigial members | `has_baseline_variance_`, `idle_5min_roll_var_`, `idle_5min_pitch_var_`, `roll_peaks_`, `pitch_peaks_`, `roll_modal_scratch_`, `pitch_modal_scratch_`. ~4 KB RAM recovered. |
| Dead Kconfig keys | `CONFIG_MONITOR_K_IDLE_X100`, `CONFIG_MONITOR_K_DISTURBED_X100`, `CONFIG_MONITOR_ABS_MIN_VAR_X10000` (Gen 1 variance-based thresholds). `CONFIG_MONITOR_CENTERLINE_MIN_AMPLITUDE_X100`, `CONFIG_MONITOR_CENTERLINE_LOBE_REVERSAL_X100` (centerline pair construction). |
| Dead MonitorConfig fields | `centerline_min_amplitude_deg`, `centerline_lobe_reversal_deg`. |
| Deprecated specs | `chebyshev-hpf-disturbance`, `posthoc-decay-detection`, `notebook-centerline-modal-analysis`. |

### 3.4 Key Algorithmic Source Files

| Algorithm | Python Reference | C++ Implementation |
|-----------|-----------------|-------------------|
| TKEO | `imu_algorithms/_detection.py` (`tkeo()`, `tkeo_streaming()`) | `monitor.cpp` (`TkeoWindow::Push()`) |
| Schmitt Trigger | `imu_algorithms/_detection.py` (`EventDetector`) | `monitor.cpp` (`DspDisturbanceDetector::Update()`) |
| Decay Onset | `imu_algorithms/_envelope.py` (`find_decay_onset_tkeo()`) | `monitor.cpp` (`FindDecayOnsetTkeo()`) |
| Natural Frequency | `imu_algorithms/_extraction.py` (`extract_natural_frequency()`) | `monitor.cpp` (`ComputeSignedAxisNaturalFrequency()`) |
| Peak-Hold Envelope | `imu_algorithms/_envelope.py` (`envelope_peak_hold()`) | `monitor.cpp` (inline in `ComputePeakHoldDamping()`) |
| OLS Damping | `imu_algorithms/_envelope.py` (`damping_from_envelope()`) | `monitor.cpp` (`ComputePeakHoldDamping()`) |
| Adaptive Comp Filter | None | `components/filter/include/adaptive_complementary_filter.hpp` |
| Calibration | `imu_algorithms/_calibration.py` | `monitor.cpp` (inline), `calibration.hpp` (NVS persistence) |
