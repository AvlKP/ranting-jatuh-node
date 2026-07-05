## Context

Logger already resolves a stable node ID through `CONFIG_LOGGER_NODE_ID` or persisted NVS generation. That node ID is used for MQTT topics and dashboard status, but `network_task.cpp` still passes `CONFIG_LOGGER_MQTT_CLIENT_ID` directly to ESP-MQTT. With the default config, every flashed node connects as `ranting-logger`, causing broker-side client ID collisions.

Constraints:

- ESP-IDF C++ build has exceptions disabled.
- Logger/network code should keep deterministic allocation and avoid heap-owned strings.
- Existing deployments may have `LOGGER_MQTT_CLIENT_ID` configured.

## Goals / Non-Goals

**Goals:**

- Make MQTT client IDs unique per resolved node ID.
- Keep client ID stable across reboots when node ID is stable.
- Preserve existing node ID resolution and topic formatting behavior.
- Provide a bounded helper that can be unit-tested without MQTT broker access.

**Non-Goals:**

- Change MQTT topic schema.
- Add broker-side registration, authentication changes, or server changes.
- Guarantee uniqueness beyond the existing node ID uniqueness model.

## Decisions

1. Add `logger::mqtt::GetClientId()`.

   `GetClientId()` will resolve the node ID through the same path as `GetNodeId()`, format a static client ID buffer, and return `const char*` for ESP-MQTT config. This keeps `network_task.cpp` from duplicating node ID formatting rules.

   Alternative considered: format directly in `EnsureMqttClient()`. Rejected because MQTT identity belongs beside `GetNodeId()` and `GetTopic()`, and a helper is easier to test.

2. Treat `CONFIG_LOGGER_MQTT_CLIENT_ID` as an optional prefix.

   If `CONFIG_LOGGER_MQTT_CLIENT_ID` is non-empty, final client ID is `<configured-client-id>-<node-id>`. If empty, final client ID is `<node-id>`. This preserves existing configured fleet/application names while ensuring per-node uniqueness.

   Alternative considered: ignore `CONFIG_LOGGER_MQTT_CLIENT_ID` entirely and use only node ID. Rejected because it would remove useful operator context from client IDs.

3. Use fixed buffers and `snprintf`.

   The helper will use a bounded static buffer sized for configured prefix plus node ID and NUL terminator. Overlong configured prefixes will be truncated by `snprintf`; node ID remains unchanged. No heap allocation or exceptions are introduced.

   Alternative considered: build with `std::string`. Rejected for embedded runtime predictability and project style.

4. Log final broker/client identity once during MQTT client init.

   `EnsureMqttClient()` should log `CONFIG_LOGGER_MQTT_URI` and `mqtt::GetClientId()` before or during client init so field logs show which identity reached the broker.

## Risks / Trade-offs

- Configured client ID no longer maps exactly to broker client ID -> document as prefix behavior and include final ID in logs.
- Truncated configured prefix could hide operator-provided suffix data -> keep node ID suffix in the generated ID and document bounded length.
- Existing brokers with ACLs keyed to exact old client ID may reject the new derived ID -> operator must update ACLs to match `<configured-client-id>-<node-id>` or set empty prefix to use node ID directly.
