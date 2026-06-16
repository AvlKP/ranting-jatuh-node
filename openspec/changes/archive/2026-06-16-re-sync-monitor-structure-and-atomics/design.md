## Context

Commit `90aa4f3` preserved logger/outbox rewrites, network cadence enforcement, the `components/runtime/` NVS component, and LSM6DS3 function-pointer conversion. However, it reverted `monitor.hpp` to its pre-refactor state (no atomics, no heap-free docs) and kept the monolithic `monitor.cpp`. Six split module files already exist on disk (`ae_detector.cpp`, `disturbance_detector.cpp`, `modal_analyzer.cpp`, `monitor_publisher.cpp`, `monitor_task.cpp`, `sample_store.cpp`) with Doxygen headers and correct implementations, but they are not compiled — `CMakeLists.txt` only lists `monitor.cpp`.

The split files were extracted from a version of `monitor.cpp` that had atomic state and snapshot safety already applied. They reference `.load()`/`.store()` on `state_` and atomic counters, so they will only compile if `monitor.hpp` is updated with `std::atomic` members.

## Goals / Non-Goals

**Goals:**
- Re-apply `std::atomic` to monitor state and diagnostic counters for cross-core-read safety on ESP32-S3 SMP.
- Re-apply the monitor file split by reducing `monitor.cpp` to a facade and compiling the six existing module files.
- Make `ae_mux_` mutable so `PendingAeEvents()` can read under critical section from const context.
- Fix `test_monitor_algorithms.cpp` to use `state_.load()`.
- Preserve every other change from HEAD (logger, LSM6DS3, runtime NVS, main.cpp, calibration handling, outbox tests).

**Non-Goals:**
- Modify logger/outbox/network implementation (HEAD version is better).
- Change `calibration.hpp` (HEAD handles `nvs_commit` inside `SetCalibrationBiases`).
- Modify `main.cpp`, `components/runtime/`, or `components/lsm6ds3/`.
- Change public Monitor API, MQTT/CSV payload schemas, or signal-processing algorithms.

## Decisions

### Keep HEAD's calibration approach

`calibration.hpp` `WriteBiases` does not call `nvs_commit`. `Monitor::SetCalibrationBiases` now calls `runtime::EnsureNvsInitialized()` followed by `WriteBiases` and an explicit `nvs_commit`. This is equivalent to our previous approach of moving `nvs_commit` into `WriteBiases`. No change needed.

### Keep HEAD's NVS init (runtime component)

The `components/runtime/` component provides `EnsureNvsInitialized()`, an idempotent wrapper around `nvs_flash_init()`. This is cleaner than our previous inline function in `main.cpp`. Monitor's `Init()` and `SetCalibrationBiases()` already call it. No change needed.

### Re-apply atomics to monitor.hpp members only

Only three members need atomic treatment:
- `state_` → `std::atomic<NodeState>` — read by dashboard from another core
- `dropped_result_events_` → `std::atomic<std::uint32_t>` — incremented in publisher, read by dashboard/verify
- `dropped_failure_events_` → `std::atomic<std::uint32_t>` — same

`pending_ae_events_` stays as plain `std::uint32_t` because it's always accessed under `ae_mux_`. Making `ae_mux_` mutable allows `PendingAeEvents()` to remain a const method.

Alternative considered: use `portENTER_CRITICAL` for all counts. Rejected because atomics are simpler for monotonically-increasing counters and don't block the other core.

### Keep existing split files as-is

The six split files on disk have verified-correct content matching HEAD's algorithm implementations. They will be compiled directly without modification. The only risk is that they were extracted from a base that had atomic `.load()`/`.store()` calls — this is exactly what we want after updating `monitor.hpp`.

### Reduce monitor.cpp by removing methods present in split files

The reduction is mechanical: delete any method body that exists in a split file. Only the facade methods remain:
- Constructor, `Init()`, `Update()`, `ReadImu()`, `ReadImuSample()`
- `CheckFailureEvents()` (shell that delegates to AE code in `ae_detector.cpp`)
- `SetCalibrationBiases()`, `PendingAeEvents()` (implementation moves from inline header to .cpp)

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| Duplicate symbols if a method is left in both monitor.cpp and a split file | Build immediately after reduction; linker will catch duplicates |
| Split files reference `monitor_internal.hpp` which lacks includes | Verified: all split files #include the necessary headers |
| Test breakage from atomic access | Only `test_monitor_algorithms.cpp` accesses `state_` directly; fix with `.load()` |
| CMakeLists ordering | Order of SRCS doesn't matter; just ensure no file is missing |

## Open Questions

None. The implementation path is deterministic: update header, reduce cpp, fix test, update cmake, build.
