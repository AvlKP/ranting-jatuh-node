# Ranting Jatuh Node

ESP32-S3 IoT node for tree branch structural monitoring. Detects failure precursors and failure events on tree branches, logs data to microSD, and publishes to an MQTT broker for remote analysis.

## Hardware

- **MCU:** ESP32-S3FH4R2 on custom PCB (pinout in `main/pins.hpp`)
- **IMU:** LSM6DS3TR-C (accelerometer + gyroscope) via I2C 400kHz
- **Storage:** microSD card via SDMMC 1-bit or SPI
- **Sensors:** Acoustic Emission (AE) analog sensor — GPIO interrupt or ADC threshold detection

## Architecture

```
main.cpp (app_main)
 ├── monitor      -- Sensor fusion, FSM, failure detection, FFT analysis
 ├── logger        -- SD card CSV logging, MQTT publishing, WiFi management
 ├── dashboard     -- HTTP debug dashboard (real-time charts, file browser)
 ├── lsm6ds3       -- LSM6DS3TR IMU driver (I2C/SPI, full register map)
 └── filter        -- Orientation filters (complementary, madgwick, kalman, EKF)
```

**Data flow:**
1. `lsm6ds3` reads accelerometer + gyroscope samples
2. `monitor` fuses sensor data (adaptive complementary filter), runs IDLE/DISTURBED state machine
3. On DISTURBED->IDLE transition: post-hoc centerline modal analysis (lobe-collapsed extrema, residual FFT, damping ratio via log-linear regression)
4. Failure events: free-fall (hardware interrupt) and acoustic emission (GPIO interrupt or ADC threshold)
5. `logger` receives results via `esp_event`, writes CSV to SD card, publishes JSON to MQTT
6. `dashboard` serves real-time HTTP visualization on port 80

**FreeRTOS tasks:**
- Monitor task: core 1, priority 5, 8KB stack
- Logger task: core 0, priority 4, 6KB stack

## Build

**Requirements:** ESP-IDF v5.5.4

```bash
# Set up ESP-IDF environment (Windows PowerShell)
. 'C:\Espressif\frameworks\esp-idf-v5.5.4\export.ps1'

# Configure (first time only)
idf.py set-target esp32s3
idf.py menuconfig

# Build, flash, monitor
idf.py build
idf.py -p COMx flash monitor
```

## Configuration

Key Kconfig options (run `idf.py menuconfig`):

**Application Storage:**
- `APP_SD_INTERFACE` — SDMMC 1-bit (default) or SPI
- `APP_SD_MOUNT_POINT` — default `/sdcard`

**WiFi & MQTT (Logger component):**
- `LOGGER_WIFI_SSID` / `LOGGER_WIFI_PASSWORD` — WiFi credentials (PSK or WPA2-Enterprise PEAP)
- `LOGGER_MQTT_URI` — broker URI (default `mqtt://broker.hivemq.com`)
- `LOGGER_MQTT_TOPIC_PARAMETERS` — default `ranting/{node_id}/parameters` (runtime-constructed, node ID from `LOGGER_NODE_ID`)
- `LOGGER_MQTT_TOPIC_FAILURES` — default `ranting/{node_id}/failures`
- `LOGGER_NODE_ID` — node identifier for topic prefix. Empty = auto-generate random adjective-noun ID (stored in NVS)
- `LOGGER_NTP_SERVER` — NTP for timestamps (default `pool.ntp.org`)

**Monitoring (Monitor component):**
- `MONITOR_IMU_RATE_HZ` — sample rate (default 26 Hz)
- `MONITOR_STORAGE_MINUTES` — disturbance buffer duration (default 5 min)
- `MONITOR_AE_MODE` — AE sensor mode: GPIO interrupt or ADC threshold
- `MONITOR_FREEFALL_THS` / `MONITOR_FF_DUR` — free-fall detection parameters
- FSM thresholds, tare, FFT options — see component Kconfig

**Verification:**
- `APP_VERIFY_ENABLE` — startup self-tests (SD, MQTT, monitor output)

`sdkconfig.defaults` sets MQTT v5 and ESP-IDF log v2.

## Implementation Status

### Complete
- LSM6DS3 IMU driver (full register map, I2C 400kHz)
- Adaptive complementary filter for roll/pitch orientation
- IDLE/DISTURBED state machine with Chebyshev HPF disturbance threshold
- Post-hoc centerline modal analysis: lobe-collapsed extrema, residual FFT, damping ratio regression
- Peak-to-peak sway metrics during disturbance
- Free-fall detection (hardware interrupt)
- Acoustic emission detection (GPIO interrupt + ADC threshold)
- SD card CSV logging (one file per date)
- JSON MQTT publishing for parameters, CSV for failures
- WiFi on-demand (connect, publish, disconnect) for power efficiency
- NTP time sync at startup
- HTTP debug dashboard with real-time charts and SD file browser
- Startup self-test/verification routines
- Tilt taring (baseline offset removal)
- Debug CSV logging mode
- MQTT v5 protocol

### Not Yet Implemented
- Production-grade wind/RDT damping estimator
- IMU FIFO usage to reduce awake time
- Temperature calibration on IMU
- Fix 0.01 deg sensor drift
- Check for conditions when disturbance occur before taring and before baseline
- Fix FFT and damping ratio calculation

## MQTT Topics

| Topic | Payload | Description |
|-------|---------|-------------|
| `ranting/{node_id}/parameters` | JSON | Periodic monitoring data (tilt, frequency, damping, sway) |
| `ranting/{node_id}/failures` | Text/CSV | Failure events (free-fall, acoustic emission) |
| `ranting/{node_id}/verify` | JSON | Startup self-test results (if verification enabled) |

**Server Subscription**: Use MQTT wildcard patterns — `ranting/+/parameters`, `ranting/+/failures`, `ranting/+/verify` — to receive data from all nodes. Extract `{node_id}` from the topic to identify the source.

See `mqtt_interface.md` for full payload schema.
