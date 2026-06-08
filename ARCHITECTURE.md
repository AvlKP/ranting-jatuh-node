# Ranting Jatuh Node Architecture

## 1. Data Processing Pipeline And Reasoning

Ranting Jatuh Node is an ESP32-S3 firmware for branch structural monitoring. It reads IMU motion and acoustic emission signals, extracts branch-state parameters, logs local data to microSD, and publishes warnings/results to MQTT.

System pipeline:

1. Boot initializes default ESP event loop, I2C bus, LSM6DS3, SD card, monitor, logger, optional dashboard, and NTP time sync.
2. Monitor task samples LSM6DS3 accel/gyro at `CONFIG_MONITOR_IMU_RATE_HZ`.
3. Raw IMU values receive static bias correction.
4. Accelerometer values feed Chebyshev Type 1 high-pass filters, one 2nd-order Direct Form II biquad per axis.
5. Accelerometer and gyroscope values feed adaptive complementary filter for roll/pitch.
6. Startup tare subtracts baseline roll/pitch offset after filter settle.
7. Monitor stores roll/pitch samples in fixed ring buffers and pushes latest samples to dashboard stream buffer.
8. HPF magnitude drives the `IDLE`/`DISTURBED` state machine.
9. State results and failure events are posted through `MONITOR_EVENT_BASE`.
10. Logger receives events through ESP event handlers, copies them into a FreeRTOS queue, writes CSV to SD, and publishes MQTT when enabled by runtime mode.

Chebyshev Type 1 HPF exists for disturbance detection, not waveform preservation. Requirement is sharp roll-off between gravity/slow tilt and dynamic branch motion. Distortion of the disturbance waveform is acceptable because the HPF output only gates state transition; modal analysis uses stored roll/pitch after orientation filtering.

Adaptive complementary filter estimates orientation while handling disturbed motion. Near 1 g, accelerometer tilt is trusted more. During strong dynamic acceleration, `|accel_magnitude - 1.0|` rises, alpha approaches 1.0, and gyro integration dominates. Reason: accelerometer tilt becomes contaminated by branch acceleration during motion.

State machine has only two states:

- `IDLE`: normal monitoring. Firmware publishes 5-minute roll/pitch mean and variance. Sway, frequency, and damping fields are zeroed.
- `DISTURBED`: dynamic motion detected. Firmware records the disturbance buffer. On buffer refresh, it publishes intermediate sway statistics. On `DISTURBED -> IDLE`, it runs final post-hoc modal analysis and publishes final event parameters.

Disturbance entry uses a single absolute HPF magnitude threshold. Exit requires `CONFIG_MONITOR_DISTURBED_EXIT_DEBOUNCE` consecutive below-threshold samples. Reason: immediate entry catches fast events; debounced exit prevents chatter.

Post-hoc modal analysis runs on stored disturbance tilt:

1. Detect raw local extrema.
2. Collapse multiple extrema inside one lobe into one representative peak/trough.
3. Build adjacent opposite-extrema centerline pairs.
4. Subtract interpolated centerline from tilt to produce residual oscillation.
5. Compute roll and pitch natural frequencies from residual FFT.
6. Compute damping ratio by linear regression on `ln(amplitude)` versus time.

FFT uses de-meaning, Hann windowing, zero-padding for short records, and Welch-style overlapping 1024-sample segments for longer records. Frequency selection is bounded by `CONFIG_MONITOR_MODAL_FREQ_MIN_HZ_X10` and `CONFIG_MONITOR_MODAL_FREQ_MAX_HZ_X10` so DC and out-of-band noise do not become reported modal frequency.

Damping estimate remains current design. It assumes usable free-decay content exists after disturbance. Known limitation from project notes: damping can be unreliable on bench data when peak-envelope assumptions reject useful peaks. Architecture keeps current post-hoc method; future ADC/DSP AE path and production damping refinements can be added later without changing MQTT schema.

Failure detection has two paths:

- Free-fall: LSM6DS3 motion event status indicates free-fall and triggers `free_fall` failure.
- Acoustic emission: current debug/development path supports GPIO rising-edge interrupt or ADC threshold. Final intended AE path is ADC plus DSP. The GPIO path assumes AE frontend output holds/pulses long enough for ISR capture.

MQTT payload model:

- Parameters publish to `ranting/{node_id}/parameters`.
- Failures publish to `ranting/{node_id}/failures`.
- Verification publishes to `ranting/{node_id}/verify`.
- Deployment intent: batch parameter publishes to save power.
- Debug intent: publish per event for faster feedback.

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
