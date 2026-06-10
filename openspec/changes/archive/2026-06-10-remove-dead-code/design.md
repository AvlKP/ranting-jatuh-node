## Context

The `monitor` component has accumulated dead code across three algorithm generations:

1. **Gen 1**: Variance-based IDLE/DISTURBED detection using `|accel_mag - 1.0|` (Kconfig keys `K_IDLE`, `K_DISTURBED`, `ABS_MIN_VAR`). Replaced by Gen 2 Chebyshev HPF design.
2. **Gen 2**: Chebyshev HPF per-axis disturbance gate (`chebyshev_hpf.hpp`). Designed but never wired into production `Monitor::Update()`. DSP detector replaced it before integration.
3. **Gen 3 (current)**: TKEO-based `DspDisturbanceDetector` + `AnalyzeImuEvent` post-hoc pipeline. This is the active production path matching `imu_algorithms/`.

The old modal analysis path (`AnalyzeModalAxis` with centerline subtraction, lobe collapse, pair envelope) sits alongside the active `AnalyzeImuEvent` path. Both compile, but only the latter drives production results. The only non-test reference to old modal output is `CONFIG_MONITOR_DEBUG_DUMP`, which currently logs never-written `roll_modal_scratch_` and `pitch_modal_scratch_` after `AnalyzeImuEvent` runs.

The `components/filter/` directory is a general-purpose library and is **out of scope**. Unused filter variants (Madgwick, Kalman, EKF, Complementary) remain.

Removing dead code reduces maintenance surface area and prevents the risk of future changes accidentally depending on untested code paths.

## Goals / Non-Goals

**Goals:**
- Remove all project-specific dead methods, types, class members, and config keys with zero production call sites in the `monitor` component
- Remove corresponding dead test code
- Remove centerline debug dump/analyzer requirements that depend on the deleted modal path
- Keep all types and members shared between dead and active code paths (`FftBinRange`, `residual_scratch_`, `peak_min_amplitude_deg`, `peak_min_spacing`)
- Each removal is a separate, reviewable, revertible commit

**Non-Goals:**
- Removing unused filter variants from `components/filter/` (general-purpose library, out of scope)
- Refactoring active code to match Python reference
- Fixing the dominant axis selection bug (`ComputeDominantAxisSway` cumulative sum vs peak-to-peak)
- Adding event classification (`classify_event`, `is_dynamic`) to C++
- Adding frequency cross-validation (ZC, PK methods)
- Replacing debug dump with a new active-pipeline TKEO/event-analysis dump format
- Changing the active algorithm pipeline in any way

## Decisions

### Decision 1: Remove in layers, verify each layer

Each category of dead code is isolated for independent review:

| Layer | Category | Review Check |
|-------|----------|-------------|
| 1 | ChebyshevHpf | Are there any call sites outside tests? |
| 2 | Shared dependency audit | Which types/config are shared with active code? |
| 3 | Old modal analysis methods | Does `ComputeAndPublish` call them? |
| 4 | Old modal analysis types | Are any referenced outside the dead methods? |
| 5 | Old natural frequency methods | Does `ComputeAndPublish` call them? |
| 6 | Vestigial Monitor members + debug dump | Are they read anywhere outside dead debug/test code? |
| 7 | Dead Kconfig keys + MonitorConfig fields | Are they referenced in production .cpp/.hpp or sdkconfig defaults? |
| 8 | Dead debug analyzer code | Does it parse only removed centerline dump tags? |
| 9 | Dead test code | Does the tested method still exist? |
| 10 | Final verification | Does `idf.py build` succeed? Are all removed identifiers gone? |

**Alternatives considered**: Single monolithic removal commit. Rejected because user requires per-item review and approval.

### Decision 2: Conservative retention of shared types

`FftBinRange` is used by both the dead `ComputeResidualNaturalFrequency` and the active `ComputeSignedAxisNaturalFrequency`. Keep.

`residual_scratch_` is used by both the dead `SubtractCenterline` and the active `ComputePeakHoldDamping`. Keep.

`MonitorConfig` fields `peak_min_amplitude_deg` and `peak_min_spacing` are read by active `ComputeSwayAndDamping` even though they were originally introduced for the dead centerline path. Keep.

`PeakList` was initially flagged as possibly shared with `ComputeSwayAndDamping`. Grep audit confirmed it is NOT used by any active code — all 7 references are in dead methods (`FindDecayRegion`, `ComputeDampingRegression`, `SelectPairEnvelope`, `DumpDebugToSD`). Safe to remove.

**Alternatives considered**: Aggressive removal then fix build errors. Rejected — too risky for a dead-code cleanup; don't want to accidentally break active sway computation.

### Decision 3: Before/after verification

Before any removal, run `idf.py build` to establish baseline. After each layer, run `idf.py build` to confirm no breakage. This catches accidental removal of shared dependencies.

**Alternatives considered**: Trusting grep-based call site analysis alone. Rejected — compiler catches missed transitive dependencies.

### Decision 4: Spec strategy

Five existing specs are deprecated via REMOVED delta specs:
- `chebyshev-hpf-disturbance`: HPF never integrated
- `posthoc-decay-detection`: Old centerline-based path replaced
- `notebook-centerline-modal-analysis`: Notebook reference, deprecated
- `debug-dump-sd`: Centerline modal debug dump depends on removed `AnalyzeModalAxis` outputs and stale scratch members
- `debug-analyzer-format`: Python centerline analyzer format depends on the removed firmware dump tags

The active specs (`imu-event-analysis`, `free-decay-analysis`, `envelope-damping-regression`) require no changes.

### Decision 5: Remove, do not rewrite debug dump

The existing debug dump is not production telemetry. It was built to inspect centerline modal analysis (`DECAY`, `PEAKS`, `COLLAPSED`, `PAIRS`, `MODAL_TIME_US`) and now conflicts with the active TKEO-based path. Rewriting it around `AnalyzeImuEvent` would be a new feature with a new file format and analyzer contract, not dead-code removal.

**Alternatives considered**: Keep `CONFIG_MONITOR_DEBUG_DUMP` and emit active-pipeline fields. Rejected for this change because it expands scope and needs new requirements for TKEO bursts, dominant-axis FFT, peak-hold envelope, and confidence diagnostics.

## Risks / Trade-offs

| Risk | Mitigation |
|------|-----------|
| Accidental removal of shared type still referenced by active code | Layer 2 audit before any type removal; `idf.py build` after each layer |
| `FftBinRange` caught in dead type purge | Explicitly excluded from removal list |
| Dead `CONFIG_MONITOR_*` keys still referenced in sdkconfig defaults or CI configs | Check `sdkconfig.defaults` before removing Kconfig entries |
| Removing debug dump violates active specs | Add REMOVED deltas for `debug-dump-sd` and `debug-analyzer-format` in this change |
| `idle_5min_roll_var_`/`pitch_var_` removed but still logged at line 359 | Remove the `ESP_LOGI` line that references them along with the members |
| Test files reference removed headers/methods | Remove corresponding test functions in `test_monitor_modal.cpp` and `test_monitor_algorithms.cpp` |
| User wants each removal reviewed — risk of slow review cadence | Each layer is independent; can be reviewed and merged separately |
