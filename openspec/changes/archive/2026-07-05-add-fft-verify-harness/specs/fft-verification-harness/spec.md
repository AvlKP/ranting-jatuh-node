## ADDED Requirements

### Requirement: Standalone FFT verification project
The system SHALL be a standalone ESP-IDF project located in a sibling directory to the node repo (`../ranting-jatuh-fft-verify`), buildable and flashable onto the same ESP32-S3FH4R2 custom PCB, with no build dependency on the node repo.

#### Scenario: Independent build
- **WHEN** the operator runs `idf.py build` in the harness project directory
- **THEN** the project builds without referencing node repo sources and produces a flashable firmware for ESP32-S3

#### Scenario: Flash and monitor
- **WHEN** the operator runs `idf.py -p <port> flash monitor`
- **THEN** the firmware flashes onto the node board and the serial monitor begins printing FFT results

### Requirement: Active-disturbance-only FFT scope
The system SHALL compute the natural frequency from a live 5 s window of the active disturbance (calibrated gyro samples collected in real time) and SHALL NOT apply any free-decay onset detection, state-machine gating, or decay-phase scoping.

#### Scenario: FFT runs on the live driven window
- **WHEN** the operator drives the node with a known oscillation during a 5 s collection window
- **THEN** the system FFTs the full 130-sample calibrated gyro window and prints the resulting frequency

#### Scenario: No decay-onset gating
- **WHEN** the system processes a 5 s window
- **THEN** it does not invoke any TKEO decay-onset detection, DISTURBED/IDLE state transition, or decay-region sub-selection

### Requirement: Firmware-faithful FFT algorithm
The system SHALL compute the natural frequency using the same algorithm as `ComputeSignedAxisNaturalFrequency`, `SelectFftBinRange`, and `ComputeDominantAxisSway` in the node: count<4 guard, >1024 tail-cap, 512/1024 fft_size selection, Hann window `0.5 - 0.5*cos(2π i/(count-1))`, mean-centering, zero-pad to fft_size, ESP-DSP `dsps_fft2r_fc32` + `dsps_bit_rev_fc32`, in-band power `real² + imag²` argmax over the configured 0.5–12 Hz band, and `frequency = max_bin * sample_rate / fft_size` with `max_bin==0 → 0.0`.

#### Scenario: Algorithm parity
- **WHEN** the harness FFT function is compared line-by-line to the node's `ComputeSignedAxisNaturalFrequency` / `SelectFftBinRange` / `ComputeDominantAxisSway`
- **THEN** the algorithmic logic (guards, windowing, padding, FFT call, power scan, bin-to-Hz conversion, dominant-axis selection by integrated peak-to-peak sway) is identical, differing only in ring-buffer-vs-linear indexing and dashboard side-effect removal

### Requirement: Firmware-faithful IMU acquisition and calibration
The system SHALL sample the LSM6DS3 at 26 Hz over I2C at 400 kHz on the node's pins (SDA=5, SCL=6, address=0x6A), using the node's `lsm6ds3` driver and I2C init, and SHALL apply identical calibration: gyro bias subtraction with biases read from NVS namespace `calib` key `imu_bias` (falling back to hardcoded defaults), plus the lid-mount x/z sign-flip under a mount-orientation config matching the node.

#### Scenario: Identical signal conditioning
- **WHEN** the harness reads a gyro sample
- **THEN** it applies `calib_g[xyz] = gyro[xyz] - bias[xyz]` with the same sign convention as `Monitor::Update` under the matching mount-orientation config

#### Scenario: Calibration bias source
- **WHEN** NVS namespace `calib` contains key `imu_bias`
- **THEN** the harness reads biases from NVS and does not write to NVS
- **WHEN** NVS does not contain the key
- **THEN** the harness falls back to the hardcoded defaults `gx=1.096412, gy=-2.593744, gz=0.414028`

### Requirement: Discrete non-overlapping 5 s windows
The system SHALL collect exactly 130 calibrated gyro samples (5 s @ 26 Hz) per window, compute the FFT once at window completion, print the result, reset the window, and begin the next window with no overlap to the previous one.

#### Scenario: Window cadence
- **WHEN** the 130th sample of a window is collected
- **THEN** the system runs dominant-axis selection + FFT, prints the result, and resets the sample counter to zero for the next independent 5 s window

### Requirement: Per-window diagnostic output
The system SHALL print, for each completed 5 s window via `ESP_LOGI`: the estimated natural frequency in Hz, the selected dominant axis name, and the per-axis peak-to-peak sway values for X, Y, and Z.

#### Scenario: Readable per-window result
- **WHEN** a 5 s window FFT completes
- **THEN** a single log line reports `fn_hz`, dominant axis, and `(sway_pp_x, sway_pp_y, sway_pp_z)` so the operator can compare to the known imposed frequency and confirm axis selection

### Requirement: Runtime safety
The system SHALL disable C++ exceptions, propagate errors via `esp_err_t`, use no heap allocation in the sampling/FFT hot path (static FFT workspace), and skip a sample with a warning on IMU read failure without advancing the window index.

#### Scenario: No heap in hot path
- **WHEN** the sampler task processes a window
- **THEN** no heap allocation occurs for the FFT workspace or window buffers

#### Scenario: IMU read failure handling
- **WHEN** an I2C gyro read fails mid-window
- **THEN** the system logs a warning, skips that sample, and does not advance the window sample counter
