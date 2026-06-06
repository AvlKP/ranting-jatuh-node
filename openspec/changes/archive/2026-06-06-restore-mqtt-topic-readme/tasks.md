## 1. Kconfig and Constants

- [x] 1.1 Add `CONFIG_LOGGER_NODE_ID` string option to `components/logger/Kconfig` (default `""`, help text explaining NVS fallback and random generation)
- [x] 1.2 Add `kTopicBasePrefix` (`"ranting/"`), `kTopicDelimiter` (`"/"`), `kMaxNodeIdLen` (32) constants to `logger_mqtt.cpp`

## 2. Node ID Generation and NVS Persistence

- [x] 2.1 Define adjective and noun word pools in `logger_mqtt.cpp` (~64 each)
- [x] 2.2 Implement `GenerateNodeId()` using `esp_random()` to pick one adjective and one noun, format as `"adj-noun"`
- [x] 2.3 Implement `ReadNodeIdFromNvs()` — open `"logger"` namespace, read `"node_id"` key, return string
- [x] 2.4 Implement `WriteNodeIdToNvs()` — open `"logger"` namespace, write `"node_id"` key
- [x] 2.5 Implement `ResolveNodeId()` — check `CONFIG_LOGGER_NODE_ID` first, then NVS, then generate; store result in static variable; return `const char*`

## 3. Topic Builder

- [x] 3.1 Implement `BuildTopic(const char* datatype)` — format `"ranting/{node_id}/{datatype}"` into static buffer, return `const char*`
- [x] 3.2 Implement `GetTopic(const char* datatype)` wrapper — cache last datatype/buffer to avoid redundant formatting

## 4. Logger Internal API Exposure

- [x] 4.1 Declare `const char* GetNodeId()` in `components/logger/include/logger_internal.hpp`
- [x] 4.2 Declare `const char* GetTopic(const char* datatype)` in `components/logger/include/logger_internal.hpp`

## 5. MQTT Publish Update

- [x] 5.1 Update `PublishParameters()` in `logger_mqtt.cpp` to use `GetTopic("parameters")` instead of `CONFIG_LOGGER_MQTT_TOPIC_PARAMETERS`
- [x] 5.2 Update `PublishFailure()` in `logger_mqtt.cpp` to use `GetTopic("failures")` instead of `CONFIG_LOGGER_MQTT_TOPIC_FAILURES`

## 6. Verify Utility Update

- [x] 6.1 Update `main/verify.cpp` to include `logger_internal.hpp` and call `GetTopic("verify")` instead of `CONFIG_APP_VERIFY_MQTT_TOPIC`
- [x] 6.2 Deprecate `CONFIG_APP_VERIFY_MQTT_TOPIC` in `main/Kconfig` (add deprecation warning in help text)

## 7. Dashboard Status Update

- [x] 7.1 Include `logger_internal.hpp` in `dashboard.cpp`
- [x] 7.2 Add `"node_id"` field to `/api/status` JSON response using `GetNodeId()`

## 8. README Rewrite

- [x] 8.1 Write project overview section: tree branch monitoring system, IoT nodes + server analysis, failure prediction and notification
- [x] 8.2 Write hardware requirements section: ESP32-S3FH4R2, LSM6DS3 IMU, microSD, acoustic emission sensor, custom PCB
- [x] 8.3 Write architecture section: component tree (monitor, logger, lsm6ds3, dashboard), communication (WiFi/MQTT, I2C, SDIO)
- [x] 8.4 Write build instructions: ESP-IDF v5.5.4 setup, `idf.py build`, `idf.py flash monitor`
- [x] 8.5 Write configuration section: key Kconfig options (WiFi, MQTT broker, sample rate, node ID) and `idf.py menuconfig`
- [x] 8.6 Write implementation status section: completed features vs pending
- [x] 8.7 Write future work section: remaining items from old TODO list (wind/storm state, IMU FIFO, temperature calibration)

## 9. Build and Verify

- [x] 9.1 Run `idf.py build` and fix any compilation errors
- [x] 9.2 Verify node ID auto-generation on first boot (check NVS partition for `"node_id"` key)
- [x] 9.3 Verify node ID persists across reboots (same ID after power cycle)
- [x] 9.4 Verify Kconfig override (`CONFIG_LOGGER_NODE_ID="test-node"` takes precedence)
- [x] 9.5 Verify MQTT topics include node ID prefix by monitoring broker
- [x] 9.6 Verify dashboard `/api/status` includes `"node_id"` field
- [x] 9.7 Verify README renders correctly on GitHub (check markdown syntax)
