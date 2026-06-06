## 1. Kconfig and Constants

- [x] 1.1 Add `CONFIG_LOGGER_NODE_ID` string option in `components/logger/Kconfig` with empty default and help text
- [x] 1.2 Add `kTopicBasePrefix = "ranting/"` and `kTopicDelimiter = "/"` constants in `logger_mqtt.cpp`
- [x] 1.3 Add `kMaxNodeIdLen = 32` constant in `logger_mqtt.cpp`

## 2. Node ID Generation and Persistence

- [x] 2.1 Add adjective and noun word lists (~64 each) in `logger_mqtt.cpp` as static const char* arrays
- [x] 2.2 Implement `GenerateNodeId()` function using `esp_random()` to pick adjective-noun pair, output to buffer
- [x] 2.3 Implement `ReadNodeIdFromNvs()` — read `node_id` key from NVS namespace `logger`, return true if found
- [x] 2.4 Implement `WriteNodeIdToNvs()` — write `node_id` string to NVS namespace `logger`
- [x] 2.5 Implement `ResolveNodeId(char* out_buf, size_t buf_size)` — priority: Kconfig → NVS → generate+store, output resolved ID

## 3. Topic Builder

- [x] 3.1 Implement `BuildTopic(char* buf, size_t buf_size, const char* node_id, const char* datatype)` producing `ranting/{node_id}/{datatype}`
- [x] 3.2 Add static buffer and wrapper `GetTopic(const char* datatype)` that caches node ID and builds topic

## 4. MQTT Publish Integration

- [x] 4.1 Update `PublishParameters()` in `logger_mqtt.cpp` to use `GetTopic("parameters")` instead of `CONFIG_LOGGER_MQTT_TOPIC_PARAMETERS`
- [x] 4.2 Update `PublishFailure()` in `logger_mqtt.cpp` to use `GetTopic("failures")` instead of `CONFIG_LOGGER_MQTT_TOPIC_FAILURES`
- [x] 4.3 Expose `GetTopic()` or `GetNodeId()` via `logger_internal.hpp` so verification module can access it

## 5. Verification Topic Update

- [x] 5.1 Update `main/verify.cpp` to use runtime topic with node ID instead of `CONFIG_APP_VERIFY_MQTT_TOPIC`
- [x] 5.2 Update `main/Kconfig` `CONFIG_APP_VERIFY_MQTT_TOPIC` default or deprecate in favor of runtime construction

## 6. Dashboard Integration

- [x] 6.1 Add `#include \"logger_internal.hpp\"` or accessor to dashboard if not already present
- [x] 6.2 Add `\"node_id\"` field in `/api/status` JSON response in `dashboard.cpp` StatusHandler using resolved node ID

## 7. Documentation

- [x] 7.1 Update `mqtt_interface.md`: change topic examples from `ranting/parameters` to `ranting/{node_id}/parameters`, add node ID section
- [x] 7.2 Update `README.md` topic table: show per-node prefix format, note server wildcard subscription
- [x] 7.3 Update Kconfig help text for `CONFIG_LOGGER_MQTT_TOPIC_PARAMETERS` and `CONFIG_LOGGER_MQTT_TOPIC_FAILURES` to reflect runtime prefix

## 8. Build and Verify

- [ ] 8.1 Run `idf.py build` to confirm compilation succeeds (requires ESP-IDF environment)
- [ ] 8.2 Verify flash and monitor: check logs show generated node ID, MQTT topics have prefix (requires hardware)
- [ ] 8.3 Verify NVS persistence: reboot device, confirm same node ID is reused (requires hardware)
- [ ] 8.4 Verify Kconfig override: set `CONFIG_LOGGER_NODE_ID="test-node"`, flash, confirm topic uses `test-node` (requires hardware)
- [ ] 8.5 Verify dashboard `/api/status` includes `node_id` field with correct value (requires hardware)
- [ ] 8.6 Verify MQTT broker receives messages on `ranting/{id}/parameters`, `ranting/{id}/failures`, `ranting/{id}/verify` (requires hardware)
