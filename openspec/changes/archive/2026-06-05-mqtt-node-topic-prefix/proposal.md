## Why

All nodes currently publish to the same flat MQTT topics (`ranting/parameters`, `ranting/failures`, `ranting/verify`). The server cannot distinguish which physical node produced which data, making multi-node deployments impossible. Adding a per-node topic prefix enables the server to route, store, and analyze data per node without ambiguity.

## What Changes

- Add a `node_id` configuration that prefixes all MQTT topics: `/ranting/{node_id}/parameters`, `/ranting/{node_id}/failures`, `/ranting/{node_id}/verify`
- Generate a random human-readable ID on first boot (adjective-noun pattern, e.g. `quiet-pine`, like Docker container names) stored in NVS to prevent conflicts in multi-node deployments
- Allow compile-time override via Kconfig (`CONFIG_LOGGER_NODE_ID`) for provisioning with known IDs
- Expose `node_id` via the dashboard status API so operators can inspect which node they're connected to
- **BREAKING**: Topic format changes from `ranting/*` to `ranting/{node_id}/*` — server must update topic subscriptions to use wildcards (`ranting/+/parameters`) or per-node topics

## Capabilities

### New Capabilities

- `node-id-topic-prefix`: Generate, persist, and use a unique node identifier to namespace all MQTT publish topics so the server can distinguish nodes in a multi-node deployment.

### Modified Capabilities

<!-- No existing specs have requirement-level changes. -->

## Impact

- **Kconfig** (`components/logger/Kconfig`): New `CONFIG_LOGGER_NODE_ID` option, defaults updated for topic configs
- **NVS**: New namespace/key to persist generated `node_id` across reboots
- **MQTT publish paths** (`components/logger/logger_mqtt.cpp`): All topic construction updated to prepend node ID prefix
- **Dashboard** (`components/dashboard/dashboard.cpp`): Status API includes `node_id` field
- **Startup verification** (`main/verify.cpp`): `CONFIG_APP_VERIFY_MQTT_TOPIC` now includes `node_id`
- **Documentation** (`mqtt_interface.md`, `README.md`): Topic format updated to show per-node prefix
- **Server-side**: Subscription patterns must change from `ranting/parameters` to `ranting/+/parameters` (or explicit per-node topics)
