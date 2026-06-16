## Why

Current firmware meets many embedded safety constraints, but key runtime bugs and review friction remain: `monitor.cpp` mixes sampling, storage, detection, modal analysis, AE handling, ISR state, and event publishing in one large unit, while the SD outbox/network path can publish active files too often. This change makes the code easier to audit and closes safety gaps found during realtime C++/ESP-IDF review.

## What Changes

- Split large firmware modules by responsibility, especially monitor and logger/network code, while preserving public behavior.
- Add explicit module-boundary contracts for monitor sampling, sample storage, modal analysis, AE detection, event publication, network strategy, and outbox ownership.
- Fix SD outbox file ownership so the network task never publishes or moves a file the logger task can still append to.
- Enforce configured MQTT publish cadence in field/on-demand mode while still allowing failure data to be uploaded promptly.
- Initialize NVS before calibration and node-ID users access it, and make calibration read/write failures visible.
- Make cross-task diagnostics/state snapshots concurrency-safe on ESP32-S3 SMP.
- Repair monitor Unity test build and add regression coverage for outbox sealing, publish cadence, NVS init order, and diagnostics access.
- Keep hot paths heap-free, bounded, and compatible with ESP-IDF v5.5.4 on ESP32-S3.

## Capabilities

### New Capabilities
- `firmware-structure`: Defines module-boundary and readability requirements for large firmware components without changing external behavior.

### Modified Capabilities
- `embedded-runtime-safety`: Add requirements for SMP-safe diagnostics/state snapshots, test-build health, and avoiding runtime heap/type-erasure in hot paths.
- `sd-upload-queue`: Add requirements for sealed upload files and active append-file exclusion.
- `network-task`: Add requirements for publish cadence enforcement and urgent failure publish behavior.
- `imu-calibration`: Add requirements for NVS initialization before calibration access and observable calibration persistence failures.
- `monitor-mutex-safety`: Extend mutex safety to snapshot/copy APIs and dashboard-visible state access.

## Impact

- Affected code: `components/monitor`, `components/logger`, `components/lsm6ds3`, `main`, and monitor/logger tests.
- Public APIs should remain source-compatible where practical; internal headers/files may be added.
- Runtime impact target: no extra heap allocation in monitor sample path, ISR path, ESP event handlers, or logger enqueue path.
- Validation: `idf.py build`, `idf.py -B build-test -D TEST_COMPONENTS=monitor build`, `idf.py size`, and hardware/runtime verification logs for stack, heap, backpressure, WiFi/MQTT, SD outbox, and calibration.
