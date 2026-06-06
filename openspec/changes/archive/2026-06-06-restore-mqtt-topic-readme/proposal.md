## Why

All nodes publish to flat, shared MQTT topics (`ranting/parameters`, `ranting/failures`) making multi-node deployment impossible since the server cannot distinguish which physical node sent which data. Additionally, the README.md is a stale 5-line TODO list from early development that does not reflect the current project state, architecture, or build instructions.

## What Changes

- Add per-node unique identifier (random adjective-noun, e.g. `quiet-pine`) persisted in NVS
- Add `CONFIG_LOGGER_NODE_ID` Kconfig option for compile-time node ID override (factory provisioning)
- Prefix all MQTT publish topics with node ID: `ranting/{node_id}/parameters`, `ranting/{node_id}/failures`
- Expose node ID in dashboard `/api/status` JSON response
- Deprecate `CONFIG_APP_VERIFY_MQTT_TOPIC`; verify.cpp uses runtime topic with node ID prefix
- Rewrite README.md with project overview, hardware requirements, architecture, build instructions, configuration, implementation status, and future work
- **BREAKING**: Server MQTT subscriptions must use wildcard pattern `ranting/+/parameters` instead of flat `ranting/parameters`

## Capabilities

### New Capabilities
- `node-id-topic-prefix`: Per-node unique identifier that prefixes all MQTT publish topics, enabling multi-node deployments with server-side node disambiguation
- `readme-documentation`: Comprehensive project README covering purpose, hardware, architecture, build instructions, configuration, implementation status, and future work

### Modified Capabilities
<!-- None: these are new capabilities, not modifications to existing spec-level behavior -->

## Impact

- `components/logger/Kconfig` — new `CONFIG_LOGGER_NODE_ID` option
- `components/logger/logger_mqtt.cpp` — node ID generation, NVS persistence, runtime topic construction
- `components/logger/logger_internal.hpp` — exposed `GetTopic()` and `GetNodeId()` API
- `main/verify.cpp` — runtime topic instead of `CONFIG_APP_VERIFY_MQTT_TOPIC`
- `main/Kconfig` — deprecate `CONFIG_APP_VERIFY_MQTT_TOPIC`
- `components/dashboard/dashboard.cpp` — `node_id` field in `/api/status`
- `README.md` — full rewrite
- Server-side — MQTT subscription wildcard pattern migration
