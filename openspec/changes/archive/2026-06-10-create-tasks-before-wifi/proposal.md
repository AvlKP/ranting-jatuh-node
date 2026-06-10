## Why

Firmware boot exhausts internal heap after WiFi Enterprise (ENT) authentication when starting `logger_task` and `monitor_task`. ENT WiFi (EAP-TLS) allocates many transient mbedTLS buffers during authentication that fragment the heap, leaving `largest_block` smaller than task stack requirements, even though total free heap is sufficient. Normal PSK WiFi works because it allocates fewer, larger blocks.

## What Changes

- Split `logger::mqtt::Init()` into a zero-WiFi `InitCore()` and a `StartWifi()` phase so FreeRTOS task stacks are allocated from clean, unfragmented heap before WiFi/lwIP/mbedTLS allocations.
- Move `logger.Start()` and `monitor.Start()` before any WiFi init or connect in the boot path, immediately after queue creation and event handler registration.
- Add heap checkpoint logging after task creation to verify the fix in both PSK and ENT configurations.
- The dashboard HTTP server task may still fail under ENT fragmentation (non-critical per existing spec), but `logger_task` and `monitor_task` will start reliably.

## Capabilities

### New Capabilities

### Modified Capabilities
- `embedded-runtime-safety`: Add requirement that critical FreeRTOS task stacks are allocated before WiFi/lwIP subsystem init to avoid heap fragmentation from authentication.

## Impact

- Affected code: `components/logger/logger_mqtt.cpp` (split Init), `main/main.cpp` (reorder boot), `components/logger/include/logger_internal.hpp` (new API surface).
- Affected systems: ESP32-S3 internal heap fragmentation behavior under WPA2-Enterprise, FreeRTOS task startup ordering, WiFi initialization lifecycle.
- No external API or payload format changes.
- **BREAKING**: `logger::mqtt::Init()` API signature changes — callers must call `InitCore()` first, create tasks, then `StartWifi()`.
