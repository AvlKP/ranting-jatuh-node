## 1. Header Update

- [x] 1.1 Add `#include <atomic>` to `monitor.hpp`.
- [x] 1.2 Change `state_` from `NodeState` to `std::atomic<NodeState>`.
- [x] 1.3 Change `dropped_result_events_` and `dropped_failure_events_` to `std::atomic<std::uint32_t>`.
- [x] 1.4 Change `ae_mux_` to `mutable portMUX_TYPE`.
- [x] 1.5 Update `GetState()`, `DroppedResultEvents()`, `DroppedFailureEvents()` getters to use `.load()`.
- [x] 1.6 Change `PendingAeEvents()` from inline getter to non-inline declaration (implementation moves to monitor.cpp).
- [x] 1.7 Add heap-free allocation policy note to Monitor class Doxygen.

## 2. Source File Reduction

- [x] 2.1 Remove `TkeoWindow` and `DspDisturbanceDetector` method implementations from `monitor.cpp` (now in `disturbance_detector.cpp`).
- [x] 2.2 Remove `AeSpectralDetector` method implementations and AE helper methods (`AeGpioIsr`, `InitAeSpectralAdc`, `ProcessAeSpectralWindow`) from `monitor.cpp` (now in `ae_detector.cpp`).
- [x] 2.3 Remove modal analysis methods from `monitor.cpp` (now in `modal_analyzer.cpp`).
- [x] 2.4 Remove `ComputeAndPublish()` and `PublishFailure()` from `monitor.cpp` (now in `monitor_publisher.cpp`).
- [x] 2.5 Remove `Start()`, `TaskLoop()`, `AeSpectralTaskLoop()`, and static task entry functions from `monitor.cpp` (now in `monitor_task.cpp`).
- [x] 2.6 Remove `PushSample()`, `GetFftData()`, `GetTiltHistory()`, `GetLatestSamples()`, `BufferSize()`, `StartIndex()`, `PhysicalIndex()` from `monitor.cpp` (now in `sample_store.cpp`).
- [x] 2.7 Add `PendingAeEvents()` implementation to `monitor.cpp` — reads `pending_ae_events_` under `ae_mux_`.

## 3. Test Fix

- [x] 3.1 Update `monitor.state_` to `monitor.state_.load()` in `test_monitor_algorithms.cpp`.

## 4. Build Configuration

- [x] 4.1 Update `components/monitor/CMakeLists.txt` SRCS to list all split files in addition to `monitor.cpp`.

## 5. Validation

- [x] 5.1 Run `idf.py build` successfully.
- [x] 5.2 Run `idf.py -B build-test -D TEST_COMPONENTS=monitor build` successfully.
- [x] 5.3 Confirm no duplicate symbol errors from linker.
