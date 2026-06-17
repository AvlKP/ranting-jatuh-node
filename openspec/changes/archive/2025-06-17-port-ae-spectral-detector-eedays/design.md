## Context

Current `HEAD` has monitor split across `monitor.cpp`, `ae_detector.cpp`, `monitor_task.cpp`, `sample_store.cpp`, `modal_analyzer.cpp`, and related files. The temp implementation under `temp/ranting-jatuh-node-eedays/ranting-jatuh-node-eedays` is monolithic and based on older commit `4250607a9b623b71ee1aa245fd2895b80e26bdbe`, so copying it whole would undo current structure and safety fixes.

The AE spectral logic that matters is concentrated in temp `AeSpectralDetector::UpdateEnergy()` and temp `sdkconfig`. Current `ae_detector.cpp` already has continuous ADC setup, FFT energy computation, detector state storage, and AE task plumbing. The port should therefore be a surgical algorithm/config/test/spec alignment, not a component replacement.

There is one important source-of-truth conflict: older OpenSpec artifacts and tests describe danger-only publication, while the temp source publishes only when both `danger_active_` and `latch_active_` are true and the publish interval allows it. The user requested the working temp implementation one-to-one, so this change treats the temp source as authoritative and updates specs/tests to match it.

## Goals / Non-Goals

**Goals:**

- Port the temp AE spectral detector update algorithm into current `components/monitor/ae_detector.cpp`.
- Preserve current split-file monitor structure, atomics, dropped-event counters, NVS/runtime integration, and free-fall debounce work.
- Make algorithm behavior match temp source one-to-one:
  - write current integrator into `gradient_ring_` before gradient calculation
  - compute oldest index as `(gradient_write_index_ + 1U) % spectral_gradient_window` once full
  - clamp negative gradient to zero
  - compute z-score and update EWMA mean/variance before danger threshold comparison
  - compute threshold after EWMA adaptation
  - require `danger_active_ && latch_active_ && time_since_last_publish_ok` for `should_publish`
  - allow `spectral_min_publish_interval_ms == 0` to disable interval suppression
- Capture temp spectral runtime configuration: spectral mode enabled, danger multiplier 15.0, jump threshold 20.0, sample rate 40000 Hz, window 256, bins 64..127, leak alpha 0.95, EWMA alpha 0.05, gradient window 20, latch duration 2000 ms, publish interval 2000 ms.
- Correct AE ADC board mapping in spec text to GPIO1 / ADC1_CH0, matching `main/pins.hpp` and `main/main.cpp`.
- Update unit tests for latch-plus-danger publish behavior and temp EWMA/ring semantics.

**Non-Goals:**

- No monolithic `monitor.cpp` restore.
- No changes to logger, MQTT topics, SD CSV format, dashboard schema, or failure payload type.
- No new warning event type or status GPIO.
- No new dependencies or Arduino compatibility layer.
- No hardware pin change unless separate PCB evidence says GPIO1 / ADC1_CH0 is wrong.

## Decisions

### Treat temp source as authoritative when code and docs conflict

The temp repository contains stale/contradictory docs and tests, but the user specifically identified the temp monitor component as working implementation. Implementation will match temp `monitor.cpp` source behavior. Specs and tests will be changed to follow that behavior rather than preserving older danger-only semantics.

Alternative considered: keep current `HEAD` danger-only publish behavior because archived design and tests say that was intended. Rejected because it does not satisfy the one-to-one temp implementation request.

### Port into `ae_detector.cpp` only

The current monitor component split is newer than the temp branch and already compiles the relevant AE helper methods from `ae_detector.cpp`. Only `AeSpectralDetector::UpdateEnergy()` needs algorithm changes, plus tests/config/specs. The ADC task, FFT energy path, persistent buffers, and `PublishFailure(FailureEvent::AcousticEmission)` path stay in place.

Alternative considered: copy temp `components/monitor/monitor.cpp` wholesale. Rejected because it would delete the current split architecture and risk reverting atomics, runtime NVS, publisher, and storage fixes.

### Preserve current reset defaults

Both current and temp implementations reset EWMA variance to `100.0f` and sigma to `10.0f`. Keep those values. Header member initializers can remain `0.01f`/`0.1f` if the detector is always reset before use, but tests should exercise `Reset()` behavior and construction behavior carefully. If implementation audit finds paths that call `UpdateEnergy()` before `Reset()`, update member initializers too.

Alternative considered: add a Kconfig initial sigma. Rejected; initial variance is part of the prototype algorithm, not a tunable runtime threshold.

### Capture tuned config in project configuration

The temp `Kconfig` defaults are unchanged from current, but temp `sdkconfig` selects spectral ADC mode and changes danger/jump thresholds to 15.0/20.0. To preserve that tuning across rebuilds, implementation should update the project configuration explicitly. Preferred path is `sdkconfig.defaults` entries if they do not conflict with project workflow; otherwise update committed `sdkconfig` and document that `sdkconfig` owns target tuning.

Alternative considered: change component `Kconfig` defaults to 15.0/20.0. Rejected because the temp component defaults did not change; the tuning is project-target config, not reusable component default.

### Keep GPIO1 / ADC1_CH0 as board mapping

`main/pins.hpp`, current `main/main.cpp`, temp `main/pins.hpp`, temp `main/main.cpp`, and commit `4250607` all map AE analog input to GPIO1 / ADC1_CH0. Existing `ae-spectral-detector` spec text saying GPIO14 / ADC1_CH3 is stale and should be corrected.

Alternative considered: change code to GPIO14 / ADC1_CH3 to satisfy stale spec. Rejected because it contradicts the actual pin header and both current/temp application code.

## Risks / Trade-offs

- Temp source may encode a bug instead of intended behavior -> Mitigation: specs/tests explicitly codify temp behavior, and implementation can be hardware-tested with spectral mode.
- Latch-plus-danger publish gate may miss danger-only events -> Mitigation: this is intentional for one-to-one temp port; thresholds remain configurable if hardware testing shows missed detections.
- Updating `sdkconfig.defaults` may alter default local builds -> Mitigation: document exact changed keys and keep GPIO/simple ADC modes available for rollback.
- `sdkconfig` is already dirty in the worktree -> Mitigation: implementation must inspect existing user edits before patching and avoid reverting unrelated changes.
- Spectral mode increases CPU/RAM use -> Mitigation: current AE task/buffer bounds remain unchanged, then run `idf.py build`, `idf.py size`, and monitor unit-test build.

## Migration Plan

1. Patch `components/monitor/ae_detector.cpp` to match temp `UpdateEnergy()` exactly.
2. Update `test_monitor_algorithms.cpp` AE spectral tests to assert latch-plus-danger publication and temp EWMA/ring timing.
3. Update project config to match temp spectral mode/tuning while preserving existing Kconfig choices for rollback.
4. Update `ae-spectral-detector` spec and archive delta to remove stale GPIO14 / ADC1_CH3 and danger-only/latch-only conflicts.
5. Build normal firmware and monitor test firmware.
6. Optional hardware validation: boot spectral mode, confirm `ae_spectral_task` starts, and verify AE failure records reach logger/MQTT outbox.

Rollback: switch `MONITOR_AE_MODE` back to GPIO or ADC threshold mode, or revert only the AE detector/config commit.

## Open Questions

None for implementation. Hardware validation can still tune thresholds later, but this change intentionally preserves temp values first.
