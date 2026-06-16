## Why

A bug-fix commit (90aa4f3) preserved the logger outbox rewrite, network cadence enforcement, runtime NVS component, and LSM6DS3 function-pointer conversion, but reverted the monitor component's structural split into focused modules and its atomic cross-core safety changes. These need to be re-applied on top of the current codebase to restore auditability and thread safety while keeping all bug fixes intact.

## What Changes

- Re-apply `std::atomic` types to `Monitor::state_`, `dropped_result_events_`, and `dropped_failure_events_` for cross-core-read safety.
- Make `ae_mux_` mutable so `PendingAeEvents()` can read under critical section from a const context.
- Re-apply the monitor structural split: reduce `monitor.cpp` to a facade (constructor, `Init`, `Update`, `ReadImu`, `CheckFailureEvents`, `SetCalibrationBiases`, `PendingAeEvents`) and compile the six already-extracted module files.
- Update `components/monitor/CMakeLists.txt` to register all split source files.
- Fix `test_monitor_algorithms.cpp` to use `state_.load()` for the atomic access.
- Restore heap-free allocation policy documentation in `monitor.hpp`.

## Capabilities

### New Capabilities

None. This change re-applies previously-approved structural and safety improvements that were reverted.

### Modified Capabilities

None. No spec-level requirement is changing. The re-applied behavior (atomic state, file split, snapshot APIs) was already captured in the now-archived `firmware-structure-and-runtime-safety` change's specs (`firmware-structure`, `monitor-mutex-safety`).

## Impact

- Affected code: `components/monitor/*.cpp`, `components/monitor/include/monitor.hpp`, `components/monitor/CMakeLists.txt`, `components/monitor/test/test_monitor_algorithms.cpp`.
- Public Monitor APIs remain unchanged.
- All other components (logger, lsm6ds3, runtime, main, dashboard) are preserved as-is from HEAD.
- Build targets: `idf.py build`, `idf.py -B build-test -D TEST_COMPONENTS=monitor build`.
