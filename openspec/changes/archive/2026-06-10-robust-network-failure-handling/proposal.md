## Why

The logger task performs synchronous WiFi+MQTT operations that block up to 45 seconds on failure. When WiFi is unreachable, the logger task — responsible for processing all sensor events and writing to SD — becomes effectively dead. Failure events (branch fell!) block on network before reaching SD. No retry backoff exists, so the task continuously burns 20–45s per failed attempt. Data accumulated during outages is silently dropped when the 32-slot pending buffer wraps.

## What Changes

- **Separate network publishing from event processing**: Move all WiFi/MQTT operations to a dedicated `network_task`, freeing the logger task to always process events and write SD without blocking.
- **Reorder failure handling**: SD write happens first (unconditionally), MQTT publish is best-effort and asynchronous.
- **Add exponential backoff on network failure**: Avoid continuous blocking retries; start at 1 min, double on failure, cap at 1 hour.
- **Use SD as the MQTT upload queue**: Unsent data persists on SD. On reconnect, the network task scans SD for unsent files and uploads them. No data loss during outages.
- **Separate network strategies by mode**: Replace `#if CONFIG_DASHBOARD_ENABLE` WiFi guards with a compile-time `NetworkStrategy` module — `PersistentStrategy` (dashboard) and `OnDemandStrategy` (field deployment).
- **Absorb `fix-mqtt-client-state-on-error`**: The `EnsureMqttClient()` cleanup fix (destroy handle on mid-init failure) is a subset of the MQTT client lifecycle rework in this change. Will be included as part of the client management refactor in the network task.

## Capabilities

### New Capabilities
- `network-task`: Dedicated FreeRTOS task for WiFi/MQTT operations with backoff, decoupled from the logger event loop.
- `sd-upload-queue`: SD-backed upload queue that persists unsent MQTT payloads and replays them on reconnect.
- `network-strategy`: Compile-time selection between persistent (dashboard) and on-demand (field) WiFi/MQTT connection management.

### Modified Capabilities
- `logger-fsm-adaptation`: Logger task no longer performs network I/O; it enqueues publish requests to the network task and writes SD synchronously.

## Impact

- Affected code: `components/logger/logger.cpp`, `components/logger/logger_mqtt.cpp`, `components/logger/include/logger_internal.hpp`
- New files: network task module, network strategy modules, SD upload queue module
- Subsumes change `fix-mqtt-client-state-on-error` (client handle cleanup is part of the new client lifecycle)
- No MQTT topic, payload format, or Kconfig API changes
- Power profile: unchanged for dashboard mode; field mode gains backoff (reduces wasted radio time during outages)
