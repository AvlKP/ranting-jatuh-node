## 1. Baseline Verification

- [x] 1.1 Run `idf.py build` to establish baseline compilation success before any removals
- [x] 1.2 Run existing unit tests to confirm all tests pass pre-removal

## 2. Remove ChebyshevHpf

- [x] 2.1 Remove `components/monitor/include/chebyshev_hpf.hpp` — verify zero production `#include` references
- [x] 2.2 Remove ChebyshevHpf test cases from `components/monitor/test/test_monitor_algorithms.cpp`
- [x] 2.3 Run `idf.py build` to confirm ChebyshevHpf removal doesn't break compilation
- [x] 2.4 **REVIEW CHECKPOINT**: Present ChebyshevHpf removal diff for user approval

## 3. Audit Shared Type Dependencies Before Type/Method Removal

- [x] 3.1 Grep `PeakList` across all production `.cpp`/`.hpp` files — confirmed dead (7 uses, all in dead methods: `FindDecayRegion`, `ComputeDampingRegression`, `SelectPairEnvelope`, `DumpDebugToSD`). Mark for removal.
- [x] 3.2 Grep `FftBinRange` — confirm used by `ComputeSignedAxisNaturalFrequency` (active). Mark as KEEP.
- [x] 3.3 Grep `residual_scratch_` — confirm used by `ComputePeakHoldDamping` (active). Mark as KEEP.
- [x] 3.4 Grep `peak_min_amplitude_deg` and `peak_min_spacing` — confirm used by `ComputeSwayAndDamping` (active). Mark config fields as KEEP.
- [x] 3.5 Grep `centerline_min_amplitude_deg` and `centerline_lobe_reversal_deg` — confirm only used by dead methods (`CollapseExtremaLobes`, `BuildCenterlinePairs`). Mark for removal.

## 4. Remove Old Modal Analysis Methods

- [x] 4.1 Remove `FindDecayRegion` method (monitor.hpp + monitor.cpp) — zero production call sites
- [x] 4.2 Remove `ComputeDampingRegression` method — only called by `AnalyzeModalAxis` (dead)
- [x] 4.3 Remove `DetectRawExtrema` method — only called by `AnalyzeModalAxis` (dead)
- [x] 4.4 Remove `CollapseExtremaLobes` method — only called by `AnalyzeModalAxis` (dead)
- [x] 4.5 Remove `BuildCenterlinePairs` method — only called by `AnalyzeModalAxis` (dead)
- [x] 4.6 Remove `SubtractCenterline` method — only called by `AnalyzeModalAxis` (dead)
- [x] 4.7 Remove `SelectPairEnvelope` method — only called by `AnalyzeModalAxis` (dead)
- [x] 4.8 Remove `ComputeResidualNaturalFrequency` method — only called by `AnalyzeModalAxis` (dead)
- [x] 4.9 Remove `AnalyzeModalAxis` method — only called by tests and `#if CONFIG_MONITOR_DEBUG_DUMP` block
- [x] 4.10 Run `idf.py build` to confirm method removal doesn't break compilation
- [x] 4.11 **REVIEW CHECKPOINT**: Present old modal analysis removal diff for user approval

## 5. Remove Dead Types (After Methods Are Gone)

- [x] 5.1 Remove `DecayRegion` struct from monitor.hpp — no remaining references after method removal
- [x] 5.2 Remove `ExtremaKind` enum, `ExtremaPoint` struct, `ExtremaList` struct — no remaining references
- [x] 5.3 Remove `CenterlinePair` struct, `CenterlinePairList` struct — no remaining references
- [x] 5.4 Remove `PeakList` struct — confirmed zero active usage (Layer 3 audit)
- [x] 5.5 Remove `ModalAxisResult` struct — only used as type of vestigial scratch members and `DumpDebugToSD`
- [x] 5.6 Run `idf.py build` to confirm type removal doesn't break compilation
- [x] 5.7 **REVIEW CHECKPOINT**: Present type removal diff for user approval

## 6. Remove Dead Natural Frequency Methods

- [x] 6.1 Remove `ComputeNaturalFrequency` declaration from monitor.hpp — declared but never defined
- [x] 6.2 Remove `ComputeAxisNaturalFrequency` method — only called by `ComputeGmagNaturalFrequency` (dead)
- [x] 6.3 Remove `ComputeGmagNaturalFrequency` method — only called by tests
- [x] 6.4 Run `idf.py build` to confirm removal doesn't break compilation
- [x] 6.5 **REVIEW CHECKPOINT**: Present natural frequency removal diff for user approval

## 7. Remove Vestigial Monitor Class Members and Debug Dump

- [x] 7.1 Remove `has_baseline_variance_` — set to true but never read
- [x] 7.2 Remove `idle_5min_roll_var_` and `idle_5min_pitch_var_` — used only in cosmetic ESP_LOGI; not consumed by any state logic. Remove the log line at monitor.cpp line 358-359 along with the members.
- [x] 7.3 Remove `roll_peaks_` and `pitch_peaks_` (PeakList, ~2KB each) — never written or read
- [x] 7.4 Remove `roll_modal_scratch_` and `pitch_modal_scratch_` (ModalAxisResult) — only read in dead `#if CONFIG_MONITOR_DEBUG_DUMP` block; never written by active code path
- [x] 7.5 Remove `#if CONFIG_MONITOR_DEBUG_DUMP` block in `ComputeAndPublish` (reads zeroed scratch data)
- [x] 7.6 Remove `DumpDebugToSD` method (monitor.hpp + monitor.cpp) and its `#if CONFIG_MONITOR_DEBUG_DUMP` guards
- [x] 7.7 Remove `MONITOR_DEBUG_DUMP` from `components/monitor/Kconfig`
- [x] 7.8 Remove centerline debug dump parsing/recompute paths from `scripts/debug_analyze.py` if they only consume removed tags (`MODAL_TIME_US`, `COLLAPSED`, `PAIRS`)
- [x] 7.9 Run `idf.py build` to confirm member/debug removal doesn't break compilation
- [x] 7.10 **REVIEW CHECKPOINT**: Present vestigial member + debug removal diff for user approval

## 8. Remove Dead Kconfig Keys and MonitorConfig Fields

- [x] 8.1 Verify `CONFIG_MONITOR_K_IDLE_X100` / `MONITOR_K_IDLE_X100` has zero references in `.cpp`/`.hpp`/`.defaults`. Remove from `components/monitor/Kconfig`.
- [x] 8.2 Verify `CONFIG_MONITOR_K_DISTURBED_X100` / `MONITOR_K_DISTURBED_X100` has zero references. Remove from Kconfig.
- [x] 8.3 Verify `CONFIG_MONITOR_ABS_MIN_VAR_X10000` / `MONITOR_ABS_MIN_VAR_X10000` has zero references. Remove from Kconfig.
- [x] 8.4 Verify `CONFIG_MONITOR_CENTERLINE_MIN_AMPLITUDE_X100` is only referenced by `MonitorConfig::centerline_min_amplitude_deg` and dead centerline methods. Remove from Kconfig and the `MonitorConfig` field after dead methods are gone.
- [x] 8.5 Verify `CONFIG_MONITOR_CENTERLINE_LOBE_REVERSAL_X100` is only referenced by `MonitorConfig::centerline_lobe_reversal_deg` and dead centerline methods. Remove from Kconfig and the `MonitorConfig` field after dead methods are gone.
- [x] 8.6 Run `idf.py build` to confirm Kconfig removal doesn't break menuconfig or compilation
- [x] 8.7 **REVIEW CHECKPOINT**: Present Kconfig removal diff for user approval

## 9. Remove Dead Test Code

- [x] 9.1 Remove dead modal analysis tests from `test_monitor_modal.cpp` (`AnalyzeModalAxis`, centerline tests, `ComputeGmagNaturalFrequency` tests, `ComputeDampingRegression` tests)
- [x] 9.2 Remove ChebyshevHpf tests from `test_monitor_algorithms.cpp`
- [x] 9.3 Run remaining tests to confirm no false failures
- [x] 9.4 **REVIEW CHECKPOINT**: Present test cleanup diff for user approval

## 10. Final Verification

- [x] 10.1 Run `idf.py build` — confirm clean compilation with zero dead code
- [x] 10.2 Run unit tests — confirm all remaining tests pass
- [x] 10.3 Grep for removed identifiers (`ChebyshevHpf`, `FindDecayRegion`, `AnalyzeModalAxis`, `ModalAxisResult`, `DecayRegion`, `ExtremaList`, `CenterlinePairList`, `PeakList`, `ComputeGmagNaturalFrequency`, `ComputeAxisNaturalFrequency`, `ComputeNaturalFrequency`, `ComputeDampingRegression`, `DetectRawExtrema`, `CollapseExtremaLobes`, `BuildCenterlinePairs`, `SubtractCenterline`, `SelectPairEnvelope`, `ComputeResidualNaturalFrequency`, `DumpDebugToSD`, `has_baseline_variance_`, `idle_5min_roll_var_`, `idle_5min_pitch_var_`, `roll_peaks_`, `pitch_peaks_`, `roll_modal_scratch_`, `pitch_modal_scratch_`, `centerline_min_amplitude_deg`, `centerline_lobe_reversal_deg`, `CONFIG_MONITOR_K_IDLE`, `CONFIG_MONITOR_K_DISTURBED`, `CONFIG_MONITOR_ABS_MIN_VAR`, `CONFIG_MONITOR_CENTERLINE_MIN_AMPLITUDE`, `CONFIG_MONITOR_CENTERLINE_LOBE_REVERSAL`, `CONFIG_MONITOR_DEBUG_DUMP`, `MONITOR_DEBUG_DUMP`, `MODAL_TIME_US`, `COLLAPSED`, `PAIRS`) to confirm zero remaining production/debug references
- [x] 10.4 Verify shared keepers still exist: `FftBinRange`, `residual_scratch_`, `peak_min_amplitude_deg`, `peak_min_spacing`
