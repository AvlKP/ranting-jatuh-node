## 1. MQTT Identity Helper

- [x] 1.1 Add `logger::mqtt::GetClientId()` declaration to `components/logger/include/logger_internal.hpp`.
- [x] 1.2 Implement `GetClientId()` in `components/logger/logger_mqtt.cpp` using resolved node ID, `CONFIG_LOGGER_MQTT_CLIENT_ID` as optional prefix, fixed storage, and bounded `snprintf`.
- [x] 1.3 Ensure generated client ID remains stable across repeated calls and keeps node ID as the uniqueness suffix.

## 2. MQTT Client Wiring

- [x] 2.1 Update `components/logger/network_task.cpp` so `mqtt_cfg.credentials.client_id` uses `logger::mqtt::GetClientId()`.
- [x] 2.2 Add MQTT init log with broker URI and final generated client ID.
- [x] 2.3 Keep existing username, password, QoS, MQTT v5, reconnect, topic, and outbox behavior unchanged.

## 3. Documentation

- [x] 3.1 Update `README.md` to document `LOGGER_MQTT_CLIENT_ID` as an optional prefix and describe final client ID derivation.
- [x] 3.2 Update `mqtt_interface.md` to document MQTT client identity as `<configured-client-id>-<node-id>` or `<node-id>` when the prefix is empty.

## 4. Tests and Validation

- [x] 4.1 Add or update Unity coverage for configured prefix plus node ID, empty prefix, and repeated lookup stability.
- [x] 4.2 Run `idf.py -B build-test -D TEST_COMPONENTS=monitor build` or nearest available logger/unit-test target.
- [x] 4.3 Run `idf.py build`.
- [x] 4.4 Run `openspec validate derive-mqtt-client-id-from-node-id --strict`.
