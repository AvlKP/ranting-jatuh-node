## Why

Approximately 400+ lines of dead code from three generations of algorithm design remain in the `monitor` component. A Chebyshev HPF that was never integrated, an old centerline-based modal analysis pipeline (replaced by TKEO-based `AnalyzeImuEvent`), vestigial natural frequency methods, unused Kconfig keys, and waste class members (~28KB RAM in scratch buffers, peak lists, and modal scratch results) all persist. This dead code increases binary size, RAM consumption, and maintenance burden (dead code polluting grep results and confusing new contributors). Remove it now while the active pipeline is clearly established in `imu_algorithms/` and `free-decay-analysis`.

The `components/filter/` directory is a general-purpose library. Unused filter variants (Madgwick, Kalman, EKF, Complementary) are **not in scope** — they remain as library code even if not currently instantiated.

## What Changes

- **Remove ChebyshevHpf class**: `chebyshev_hpf.hpp` and its test coverage
- **Remove old modal analysis pipeline**: `FindDecayRegion`, `AnalyzeModalAxis`, `DetectRawExtrema`, `CollapseExtremaLobes`, `BuildCenterlinePairs`, `SubtractCenterline`, `SelectPairEnvelope`, `ComputeResidualNaturalFrequency`, `ComputeDampingRegression` methods
- **Remove old modal analysis types**: `DecayRegion`, `ExtremaKind`/`ExtremaPoint`/`ExtremaList`, `CenterlinePair`/`CenterlinePairList`, `PeakList`, `ModalAxisResult`
- **Remove vestigial Monitor members**: `has_baseline_variance_`, `idle_5min_roll_var_`, `idle_5min_pitch_var_`, `roll_peaks_`, `pitch_peaks_`, `roll_modal_scratch_`, `pitch_modal_scratch_`
- **Remove dead natural frequency methods**: `ComputeNaturalFrequency` (declaration only, never defined), `ComputeAxisNaturalFrequency`, `ComputeGmagNaturalFrequency`
- **Remove dead Kconfig keys**: `CONFIG_MONITOR_K_IDLE_X100`, `CONFIG_MONITOR_K_DISTURBED_X100`, `CONFIG_MONITOR_ABS_MIN_VAR_X10000`, `CONFIG_MONITOR_CENTERLINE_MIN_AMPLITUDE_X100`, `CONFIG_MONITOR_CENTERLINE_LOBE_REVERSAL_X100`
- **Remove dead MonitorConfig fields**: `centerline_min_amplitude_deg`, `centerline_lobe_reversal_deg`
- **Remove dead test code**: Tests for removed methods in `test_monitor_modal.cpp` and `test_monitor_algorithms.cpp`
- **Remove centerline debug dump feature**: `CONFIG_MONITOR_DEBUG_DUMP`, `DumpDebugToSD`, debug-dump call site in `ComputeAndPublish`, and Python analyzer centerline-format requirements. Current dump path reads never-written `roll_modal_scratch_`/`pitch_modal_scratch_` after the active `AnalyzeImuEvent` path runs.
- **Keep `FftBinRange`**: Shared with active `ComputeSignedAxisNaturalFrequency`
- **Keep `residual_scratch_`**: Shared between dead `SubtractCenterline` (removed) and active `ComputePeakHoldDamping`
- **Keep `peak_min_amplitude_deg`, `peak_min_spacing`** in `MonitorConfig`: Read by active `ComputeSwayAndDamping`

## Capabilities

### Modified Capabilities
- `chebyshev-hpf-disturbance`: Superseded by `imu-event-analysis` (TKEO-based DSP detector). Remove spec as the HPF was never integrated into production.
- `posthoc-decay-detection`: Superseded by `free-decay-analysis` (TKEO decay onset + per-axis FFT + peak-hold envelope damping). Remove spec as the centerline-based lobe collapse method is replaced.
- `notebook-centerline-modal-analysis`: Mark as historical reference only; centerline approach deprecated in favor of TKEO-based pipeline.
- `debug-dump-sd`: Remove centerline modal debug dump requirements. The existing dump format depends on `AnalyzeModalAxis` and never-written modal scratch members.
- `debug-analyzer-format`: Remove Python centerline analyzer requirements tied to the removed firmware dump format.

## Impact

- **Affected files**: `components/monitor/monitor.hpp`, `components/monitor/monitor.cpp`, `components/monitor/include/chebyshev_hpf.hpp`, `components/monitor/test/test_monitor_modal.cpp`, `components/monitor/test/test_monitor_algorithms.cpp`, `components/monitor/Kconfig`, `scripts/debug_analyze.py`
- **Breaking changes**: Production monitoring pipeline unchanged. Debug dump/analyzer capability is removed because it depends on deprecated centerline modal analysis and stale scratch data.
- **Risk**: `residual_scratch_`, `FftBinRange`, `peak_min_amplitude_deg`, `peak_min_spacing` are shared between dead and active code paths. Each removal must be verified to not break active code. Each removal task requires manual review and approval.
- **Specs deprecated**: `chebyshev-hpf-disturbance`, `posthoc-decay-detection`, `notebook-centerline-modal-analysis` (historical), `debug-dump-sd`, `debug-analyzer-format`
