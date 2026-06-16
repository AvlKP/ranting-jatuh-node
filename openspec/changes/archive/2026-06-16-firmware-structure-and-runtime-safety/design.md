## Context

The normal firmware build targets ESP32-S3 with ESP-IDF v5.5.4, C++ exceptions disabled, RTTI disabled, and no heap allocation in monitor/logger hot paths. Existing specs already require bounded tasks, observable backpressure, static buffers, phased WiFi initialization, and SD-first MQTT persistence.

The current implementation still has review and runtime-safety gaps:

- `components/monitor/monitor.cpp` holds sampling, state machine, sample storage, modal analysis, AE GPIO/ADC handling, event publication, and dashboard snapshot support in one large translation unit.
- `components/logger/outbox.cpp` lets the network task scan files that the logger task can still append to.
- `components/logger/network_task.cpp` treats logger notifications as publish triggers, so parameter files can upload far more often than `CONFIG_LOGGER_WIFI_PERIOD_HOURS`.
- Calibration reads/writes NVS before main performs a central NVS init, and writes do not have an explicit commit/diagnostic contract.
- Dashboard/status paths read monitor/logger state across ESP32-S3 cores without a clear atomic or locked snapshot contract.
- Monitor Unity tests currently fail to build because of unavailable Unity float comparison macros.

## Goals / Non-Goals

**Goals:**

- Reduce monitor/logger file size by splitting code along ownership and realtime boundaries.
- Preserve existing public behavior and MQTT payload schema unless a spec explicitly changes it.
- Make SD outbox ownership safe: active append file is never published or moved.
- Enforce on-demand publish cadence for parameter batches while preserving prompt failure upload after backoff allows it.
- Make NVS initialization and calibration persistence deterministic and observable.
- Make dashboard-visible state and counters safe on ESP32-S3 SMP.
- Repair and extend tests so future structure changes are guarded.

**Non-Goals:**

- Replace the signal-processing algorithms or retune thresholds.
- Add deep sleep or complex power-state orchestration.
- Change MQTT topic format or payload fields.
- Replace ESP-IDF event loop, FreeRTOS tasks, FatFS, or ESP-MQTT.
- Rewrite dashboard UI beyond API changes required for safer snapshots.

## Decisions

### Split by ownership, not line count

`Monitor` remains the public facade, but implementation moves into focused units:

```text
components/monitor/
  monitor_task.cpp             FreeRTOS task start/loop and per-sample orchestration
  sample_store.cpp/.hpp        rolling buffers, short pre-trigger buffer, snapshots
  disturbance_detector.cpp     TKEO window and Schmitt detector
  modal_analyzer.cpp/.hpp      decay onset, dominant axis, FFT, damping
  ae_detector.cpp/.hpp         GPIO/ADC/spectral AE handling and ISR/task state
  monitor_publisher.cpp        ESP event publication and drop counters
```

`monitor.hpp` should expose only application-facing types and `Monitor`. Internal headers may expose small testable classes. This keeps review paths narrow: modal analysis can be audited without ISR or dashboard code.

Alternative considered: keep one file and add comments. Rejected because comments do not reduce ownership overlap or test friction.

### Use snapshot APIs for cross-core reads

Monitor and logger counters/state used by dashboard or verification should be returned through snapshot methods that use atomics or bounded critical sections. Direct reads of mutable counters from another core should be removed.

For simple monotonically increasing counters, `std::atomic<std::uint32_t>` is acceptable if lock-free on target. For compound snapshots, use a bounded mutex/critical section and copy into caller-owned storage.

Alternative considered: rely on aligned 32-bit reads. Rejected because the code also exposes compound state, and the contract should be explicit.

### Seal outbox parameter files before upload

The outbox owns one active parameter file. Logger appends only to that active file. Network scans only sealed files plus failure files. A seal operation clears the active parameter filename and makes that file eligible for upload. On reboot, all existing pending files are considered sealed because no task still owns them.

Failure files remain immutable per event and eligible for urgent upload immediately after write. Parameter files are sealed only when the configured publish period is due or when recovering prior-boot files.

Outbox functions that read or mutate active filename/path state should be serialized with a static FreeRTOS mutex or equivalent bounded lock. Holding that lock across FatFS append/rename is acceptable because outbox is not in the IMU sample hot path, and logger already performs SD I/O.

Alternative considered: create unique filename per parameter event. Rejected because it increases directory churn and upload overhead.

### Treat network notifications as hints

`network_task::EnqueueNotify()` should wake the network task, but the task decides whether to connect based on:

- backoff state,
- pending failure files,
- sealed parameter files,
- parameter publish period deadline,
- current network strategy.

In on-demand field mode, parameter-only data must not trigger WiFi before the period. Failure files can trigger an urgent cycle when backoff is not active. In dashboard/persistent mode, the task may publish more aggressively because WiFi is already up.

Alternative considered: stop notifying the network task on parameter append. Rejected because notifications are still useful when period has just elapsed or failure data arrives.

### Initialize NVS centrally before subsystem users

`app_main` should perform one early NVS init before constructing or initializing monitor/logger subsystems that access calibration or node identity. Subsystems may still tolerate repeated `nvs_flash_init()` returning an already-initialized state, but they should not be responsible for first init ordering.

Calibration writes should call `nvs_commit()` and return/log the final `esp_err_t`. Calibration read should treat only `ESP_ERR_NVS_NOT_FOUND` as a zero-bias default; other failures are diagnostics.

Alternative considered: keep NVS init inside each subsystem. Rejected because ordering is already cross-cutting and repeated init hides failures.

### Keep realtime C++ subset explicit

Hot path code should continue using fixed-capacity storage, function pointers or static adapters instead of `std::function`, no exceptions, no RTTI, and no runtime heap allocation. Any unavoidable init-time heap use, such as ESP-MQTT internals, remains outside monitor sample/update and logger enqueue paths.

The LSM6DS3 transport callbacks should become plain function pointers or a small fixed callback struct to avoid type-erasure allocation and hidden dispatch cost in the sample loop.

### Repair tests before refactor

The first implementation step should make `idf.py -B build-test -D TEST_COMPONENTS=monitor build` compile. Then add tests around:

- active parameter file exclusion,
- seal operation,
- parameter publish cadence,
- urgent failure publish,
- NVS init/commit behavior through fakes where possible,
- SMP-safe snapshots/counters.

This protects the refactor from becoming behavior drift.

## Risks / Trade-offs

- Splitting files can create too many internal headers -> keep internal APIs small and grouped by ownership.
- Locking outbox around FatFS operations can delay network scans -> network task is lower priority than logger, and this path is not IMU realtime.
- Sealed parameter semantics can delay fresh parameters when a failure triggers urgent upload -> desired for battery and active-file safety.
- Atomic counters can increase code size slightly -> acceptable for correctness on dual-core ESP32-S3.
- Central NVS init can change boot failure behavior -> log exact `esp_err_t` and fail early only when persistence is required for correctness.
- Refactor can break tests that use `#define private public` -> prefer testing new internal classes directly and reduce private-field test dependence.

## Migration Plan

1. Repair monitor Unity test build without changing application behavior.
2. Add outbox/network tests for sealed files and publish cadence.
3. Add early NVS init and calibration diagnostics.
4. Add atomic/snapshot diagnostics.
5. Split monitor internals while preserving `Monitor` public API and existing MQTT/log payloads.
6. Split logger/network shared WiFi core only after outbox behavior is covered.
7. Run `idf.py build`, monitor test build, `idf.py size`, then hardware validation for stack/heap, SD, WiFi/MQTT, AE/free-fall, and calibration.

Rollback strategy: each split should be behavior-preserving and can be reverted module-by-module. Keep old public APIs until all callers migrate.

## Open Questions

- Should urgent failure upload include already-sealed parameter files in the same cycle, or failures only?
- Should on-demand mode upload parameters exactly at period boundary or at first network wake after boundary?
- Should calibration write failure be fatal during boot when hardcoded fallback biases are supplied?
