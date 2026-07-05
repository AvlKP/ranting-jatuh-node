## Why

Nodes currently use the same `CONFIG_LOGGER_MQTT_CLIENT_ID` default, so two devices can collide at the MQTT broker even when their publish topics include unique node IDs. Client identity needs to follow the resolved node identity so every node connects with a distinct MQTT client ID.

## What Changes

- Derive the MQTT client ID at runtime from the resolved logger node ID.
- Keep the client ID deterministic across reboots when `CONFIG_LOGGER_NODE_ID` or persisted NVS node ID is unchanged.
- Preserve existing broker URI, username, password, QoS, and topic behavior.
- Update docs/tests so configured and auto-generated node IDs also imply unique MQTT client IDs.

## Capabilities

### New Capabilities

- `mqtt-client-identity`: MQTT client ID behavior derived from node identity.

### Modified Capabilities

- `node-id-topic-prefix`: Node ID is no longer only a topic/status identifier; it also determines MQTT client identity.

## Impact

- Affected code: logger MQTT client setup in `components/logger/network_task.cpp`, node ID helper in `components/logger/logger_mqtt.cpp` or adjacent logger internals, and related tests.
- Affected config/docs: `LOGGER_MQTT_CLIENT_ID` Kconfig behavior or removal path, README, and MQTT interface documentation.
- No new external dependencies.
