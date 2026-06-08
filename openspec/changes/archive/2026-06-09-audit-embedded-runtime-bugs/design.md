## Context

Firmware runs on ESP32-S3 with ESP-IDF v5.5.4, FreeRTOS tasks, ESP event loop callbacks, an HTTP dashboard, MQTT logging, SD card storage, GPIO/ADC acoustic emission input, and monitor DSP. The monitor has already had one stack-related issue addressed by moving modal-analysis scratch objects from automatic storage into persistent members, but similar implementation hazards can remain across modules.

Current risk areas:

- `monitor_task` has 8192-byte stack and runs IMU sampling plus occasional DSP/event publication.
- `logger_task` has 6144-byte stack and formats CSV/JSON, writes SD, and publishes MQTT.
- Dashboard HTTP server stack is manually set to 12288 bytes and `/api/status` still allocates multiple automatic buffers for stream/history/FFT/MQTT log JSON.
- ESP event handlers copy monitor results and enqueue logger events; they must not block or hold locks for long periods.
- GPIO ISR only sets a `volatile bool`; ISR/task visibility should be audited for missed events and data-race assumptions.
- `sdkconfig` has FreeRTOS stack canary enabled, but compiler stack checking is disabled. Verification logs are optional.

## Goals / Non-Goals

**Goals:**

- Find and fix confirmed stack overflow, near-overflow, buffer bound, lock-order, ISR, event-handler, queue, watchdog, and hot-path heap issues.
- Keep fixes deterministic: no dynamic allocation in sampling, ISR, event handlers, or recurring monitor/logger hot paths unless already inside ESP-IDF APIs and measured acceptable.
- Quantify stack and RAM impact with `idf.py size`, map/build output, and runtime high-water marks.
- Add assertions or static checks for Kconfig-derived sizes that can make buffers too large for stack/RAM.
- Preserve monitor/logger/dashboard externally observable behavior.

**Non-Goals:**

- Redesigning monitor algorithms, modal analysis math, MQTT topics, dashboard UI, or SD file formats.
- Adding deep-sleep power policy.
- Replacing ESP-IDF services such as esp_event, esp_http_server, ESP-MQTT, or VFS FAT.
- Proving worst-case timing formally; this is a pragmatic firmware audit with measured runtime evidence.

## Decisions

**Decision 1: Audit before changing stack sizes**

Treat task stack increases as last-resort fixes. First identify large automatic objects, recursive/unbounded calls, and long call chains. Move recurring large scratch buffers to object/static storage or stream output in smaller chunks when that reduces risk without increasing shared-state complexity.

Alternatives considered:

| Alternative | Verdict | Rationale |
|---|---|---|
| Increase every task stack | Rejected | Hides root causes and consumes scarce internal RAM. |
| Move all buffers to heap | Rejected | Adds allocation failure and fragmentation risk. |
| Targeted storage changes plus measurement | Accepted | Keeps RAM use explicit and verifies actual risk. |

**Decision 2: Runtime evidence must include stack high-water marks**

Use existing `verify::LogStackHighWatermark` where available and extend diagnostics for named tasks that can be queried. Dashboard and ESP service tasks should be measured through FreeRTOS task handles or task listing support where practical. Acceptance requires observed margin after exercising idle sampling, disturbance analysis, dashboard status/download, SD writes, MQTT publish, and failure events.

**Decision 3: Keep ISR and event handlers minimal**

GPIO ISR paths shall only record events using ISR-safe primitives or lock-free state. ESP event handlers shall copy bounded payloads and enqueue/defer work with zero wait time. File I/O, MQTT publish, DSP, JSON streaming, and blocking mutex waits remain in task context.

**Decision 4: Add compile-time guards for Kconfig-derived buffers**

`kStorageSamples`, FFT sizes, short buffers, logger queue items, MQTT log sizes, and dashboard query buffers should have `static_assert` or equivalent validation when they affect static storage, automatic storage, queue item size, or event payload size. Bounds should fail at build time before invalid Kconfig can produce unusable firmware.

**Decision 5: Fix only confirmed defects in this change**

The audit will record candidates, but implementation edits should be limited to hazards confirmed by code inspection, build output, tests, or runtime measurement. Non-critical observations become documented follow-up notes instead of broad refactors.

## Risks / Trade-offs

- Stack high-water marks depend on exercised paths -> Mitigate with a scripted/manual checklist that hits monitor, logger, dashboard, SD, MQTT, and failure paths.
- Moving buffers to persistent storage increases `.bss`/static RAM -> Mitigate with `idf.py size` before/after and use the smallest lifetime that avoids stack risk.
- Instrumentation can perturb timing and stack use -> Mitigate by keeping diagnostics behind existing verification/debug config where possible.
- HTTP/dashboard diagnostics may consume RAM during debug sessions -> Mitigate by bounding response chunks and avoiding full-response JSON assembly.
- Hardware-dependent validation needs ESP32-S3 board, IMU, SD card, WiFi/MQTT reachability, and AE input path -> Mitigate by separating build/unit checks from hardware runtime checks in tasks.
