## 1. Add @deprecated Tags to Superseded Monitor Code

- [x] 1.1 Add `@deprecated` Doxygen tag to `ChebyshevHpf` class in `chebyshev_hpf.hpp`: "Superseded by TKEO-based DspDisturbanceDetector. Legacy artifact from early HPF-based disturbance detection architecture."
- [x] 1.2 Add `@deprecated` Doxygen tags to dead private methods in `monitor.hpp`: `FindDecayRegion`, `AnalyzeModalAxis`, `DetectRawExtrema`, `CollapseExtremaLobes`, `BuildCenterlinePairs`, `SelectPairEnvelope`, `SubtractCenterline`, `ComputeResidualNaturalFrequency`, `ComputeDampingRegression`, `ComputeNaturalFrequency` (declaration), `ComputeAxisNaturalFrequency`, `ComputeGmagNaturalFrequency`
- [x] 1.3 Add `@deprecated` Doxygen tags to dead types in `monitor.hpp`: `DecayRegion`, `ExtremaKind`, `ExtremaPoint`, `ExtremaList`, `CenterlinePair`, `CenterlinePairList`, `ModalAxisResult`
- [x] 1.4 Add `@deprecated` inline comments to dead `Monitor` members: `has_baseline_variance_`, `roll_peaks_`, `pitch_peaks_`, `roll_modal_scratch_`, `pitch_modal_scratch_`
- [x] 1.5 Add `@deprecated` comments to dead Kconfig keys in `components/monitor/Kconfig`: `CONFIG_MONITOR_K_IDLE_X100`, `CONFIG_MONITOR_K_DISTURBED_X100`, `CONFIG_MONITOR_ABS_MIN_VAR_X10000`
- [x] 1.6 Leave `components/filter/` files (Madgwick, Kalman, EKF, Complementary) untouched — generic DSP building blocks, not project-specific

## 2. Document monitor Component (C++)

- [x] 2.1 Add `@file` docstring to `monitor.hpp` describing the Monitor class role in the signal processing pipeline
- [x] 2.2 Add Doxygen docstrings to `Monitor` class public API: `Init`, `Start`, `Update`, `ReadImuSample`, `GetFftData`, `GetTiltHistory`, `GetLatestSamples`, `GetState`, `GetTaskHandle`, `SetCalibrationBiases`
- [x] 2.3 Add Doxygen docstrings to `TkeoWindow` and `DspDisturbanceDetector` with TKEO formula and state machine description
- [x] 2.4 Add `@see` cross-references to Python `imu_algorithms/_detection.py` for TKEO and state machine
- [x] 2.5 Add Doxygen docstrings to `MonitorConfig`, `StreamSample`, `MonitorResult`, `NodeState`, `FailureEvent`, `FailureResult`
- [x] 2.6 Add inline `///<` comments to all enum values and struct fields with units
- [x] 2.7 Add `@file` docstring to `monitor_events.hpp`
- [x] 2.8 Add `@file` docstring to `calibration.hpp` with calibration workflow description
- [x] 2.9 Add algorithm derivation comments in `monitor.cpp` for `FindDecayOnsetTkeo` and `ComputePeakHoldDamping`

## 3. Document filter Component (C++)

- [x] 3.1 Add `@file` docstring to `adaptive_complementary_filter.hpp` describing the self-tuning fusion algorithm
- [x] 3.2 Add `@file` docstring to `filter_math.hpp` with radians/degrees helper documentation

## 4. Document lsm6ds3 Component (C++)

- [x] 4.1 Add `@file` docstring to `lsm6ds3.hpp` describing the driver's role as I2C transport layer
- [x] 4.2 Add Doxygen docstrings to `Lsm6ds3` class, `Config`, `ImuConfig`, and all public methods
- [x] 4.3 Add inline `///<` comments to register configuration enums and struct fields

## 5. Document logger Component (C++)

- [x] 5.1 Add `@file` docstring to `logger.hpp` describing Logger's role in SD storage and MQTT publishing
- [x] 5.2 Add Doxygen docstrings to `Logger` class public API and `Config` struct
- [x] 5.3 Add `@file` docstring to `mqtt_log.hpp` with MQTT topic and payload format reference
- [x] 5.4 Add inline comments in `logger.cpp` for event handler, queue, and batching logic

## 6. Document dashboard Component (C++)

- [x] 6.1 Add `@file` docstring to dashboard header describing the HTTP debug server
- [x] 6.2 Add Doxygen docstrings to `Dashboard` class and configuration struct

## 7. Document main Application (C++)

- [x] 7.1 Add `@file` docstring to `main.cpp` describing boot sequence and component initialization order
- [x] 7.2 Add `@file` docstring to `pins.hpp` with pin assignment table and PCB reference
- [x] 7.3 Add `@file` docstring to `raw_logger_main.cpp` describing the alternate raw data collection mode

## 8. Document Python Reference Implementation

- [x] 8.1 Add module-level docstring to `imu_algorithms/__init__.py` with package purpose and usage example
- [x] 8.2 Add Google-style docstrings to all public functions in `_detection.py`: `tkeo`, `tkeo_streaming`, `classify_event`, `is_dynamic`
- [x] 8.3 Add Google-style docstrings to `EventDetector` class and its `reset`, `process_sample` methods
- [x] 8.4 Add Google-style docstrings to all public functions in `_envelope.py`: `envelope_peak_hold`, `find_decay_onset_tkeo`, `damping_from_envelope`
- [x] 8.5 Add Google-style docstrings to private helpers in `_envelope.py`: `_tkeo_pos`, `_local_maxima_indices`
- [x] 8.6 Add Google-style docstrings to all public functions in `_extraction.py`: `extract_natural_frequency`, `extract_frequency_zc`, `extract_frequency_pk`, `extract_active_region`, `extract_active_sway`, `extract_tilt`, `run_pipeline`
- [x] 8.7 Add Google-style docstrings to `Pipeline` class and its methods in `_extraction.py`
- [x] 8.8 Add dataclass field documentation for `Event`, `EventResult` in `_extraction.py`
- [x] 8.9 Add module-level docstring to `_calibration.py` with bias computation methodology
- [x] 8.10 Add Google-style docstrings to `calibrate` and `calibrated_gmag` in `_calibration.py`
- [x] 8.11 Add module-level docstrings to `_io.py` and `_ringbuffer.py`
- [x] 8.12 For Python-only reference functions (`classify_event`, `is_dynamic`, `extract_active_region`, `extract_frequency_zc`, `extract_frequency_pk`): note in docstring they are reference implementations intentionally not yet ported to C++

## 9. Verification

- [x] 9.1 Verify all Python docstrings render correctly with `help()` on each module
- [x] 9.2 Verify `@deprecated` tags reference correct replacement APIs by name
- [x] 9.3 Confirm `components/filter/` files (except adaptive_complementary_filter.hpp and filter_math.hpp) are untouched
