# doxygen-documentation Specification

## Purpose
Doxygen-style documentation comments for all C++ public API surfaces, algorithm functions, and data structures across the firmware components. Includes @deprecated tags on vestigial project-specific code to signal superseded design paths.

## ADDED Requirements

### Requirement: Every public header SHALL have a @file block
Every `.hpp` file under `components/` and `main/` SHALL begin with a `@file` Doxygen comment describing the module's purpose, its role in the system architecture, and the key classes or functions it exports.

#### Scenario: Header with @file documentation
- **WHEN** a developer opens `monitor.hpp`
- **THEN** the first lines after `#pragma once` SHALL contain a `@file monitor.hpp` block
- **THEN** the block SHALL include `@brief` describing the Monitor class role
- **THEN** the block SHALL include `@ingroup` tag for the monitor component

### Requirement: Every active class and struct SHALL have @brief and member documentation
Every active class and struct in public headers SHALL have a `@brief` description, and all public methods and member variables SHALL document their purpose, parameters, and return values.

#### Scenario: Class with documented methods
- **WHEN** a developer opens `monitor.hpp`
- **THEN** `class Monitor` SHALL have a `@brief` tag describing its responsibility
- **THEN** `bool Init()` SHALL document `@return` values (true/false meaning)
- **THEN** `bool Update(float dt_s)` SHALL document `@param dt_s` meaning and `@return` meaning
- **THEN** `NodeState GetState()` SHALL document `@return` value semantics

#### Scenario: Struct with documented fields
- **WHEN** a developer opens `monitor_events.hpp`
- **THEN** each enum value in `MonitorEventId` SHALL have an inline `///<` comment
- **THEN** each field in `MonitorResult` SHALL have an inline `///<` comment describing the physical quantity and units

### Requirement: Algorithm functions SHALL document their math
Every function implementing a signal processing algorithm SHALL document the mathematical formula it implements, either inline or in a `@note` block. Functions whose algorithm originates from the Python reference in `imu_algorithms/` SHALL include a `@see` reference to the corresponding Python function.

#### Scenario: TKEO function documentation
- **WHEN** a developer reads `TkeoWindow::Push`
- **THEN** the docstring SHALL include the formula `psi[n] = x[n]^2 - x[n-1] * x[n+1]`
- **THEN** the docstring SHALL include `@see imu_algorithms/_detection.py::tkeo_streaming`

#### Scenario: Damping regression documentation
- **WHEN** a developer reads `ComputePeakHoldDamping`
- **THEN** the docstring SHALL include the formula `zeta = -slope / (2 * pi * fn)`
- **THEN** the docstring SHALL explain the OLS log-fit on peak-hold envelope
- **THEN** the docstring SHALL reference `@see imu_algorithms/_envelope.py::damping_from_envelope`

### Requirement: Enum and constant values SHALL be documented
All `enum`, `enum class`, and `constexpr` constants in public headers SHALL document their meaning and valid range.

#### Scenario: Documented enum
- **WHEN** a developer opens `monitor.hpp`
- **THEN** `NodeState::IDLE` SHALL have `///< No disturbance detected`
- **THEN** `NodeState::DISTURBED` SHALL have `///< Dynamic motion in progress`
- **THEN** `FailureEvent::FreeFall` SHALL have `///< LSM6DS3 free-fall interrupt triggered`
- **THEN** `DecayQuality` enum SHALL document the criteria for each level

### Requirement: Implementation files SHALL document non-obvious logic
`.cpp` files SHALL document non-obvious design choices, algorithmic steps, and rationale for constants using inline comments. Headers alone are not sufficient for complex algorithm implementations.

#### Scenario: FFT implementation documentation
- **WHEN** a developer reads `ComputeSignedAxisNaturalFrequency`
- **THEN** comments SHALL explain the Hann window application
- **THEN** comments SHALL explain the frequency band selection rationale
- **THEN** comments SHALL explain the zero-padding and Welch overlap strategy

### Requirement: Superseded project-specific code SHALL have @deprecated tags
Project-specific code that was superseded by newer implementations SHALL receive `@deprecated` Doxygen tags explaining the replacement. This includes the ChebyshevHpf class, the old monitor modal analysis methods and their supporting types, dead class members, and dead Kconfig keys.

#### Scenario: @deprecated on superseded class
- **WHEN** a developer opens `chebyshev_hpf.hpp`
- **THEN** the class SHALL have `@deprecated Superseded by TKEO-based DspDisturbanceDetector. Legacy artifact from early architecture.`
- **THEN** the class SHALL remain in the file (not deleted)

#### Scenario: @deprecated on old monitor methods
- **WHEN** a developer opens `monitor.hpp`
- **THEN** `FindDecayRegion` SHALL have `@deprecated Superseded by AnalyzeImuEvent(). Uses TKEO decay onset + signed-axis FFT.`
- **THEN** `AnalyzeModalAxis` and all methods it calls SHALL have similar `@deprecated` tags
- **THEN** Dead structs (`DecayRegion`, `ExtremaKind`, `ExtremaPoint`, `ExtremaList`, `CenterlinePair`, `CenterlinePairList`, `ModalAxisResult`) SHALL have `@deprecated` tags referencing the replacement types
- **THEN** Dead class members (`has_baseline_variance_`, `roll_peaks_`, `pitch_peaks_`, `roll_modal_scratch_`, `pitch_modal_scratch_`) SHALL have inline `///< @deprecated` comments

#### Scenario: @deprecated on dead Kconfig keys
- **WHEN** a developer opens `components/monitor/Kconfig`
- **THEN** `CONFIG_MONITOR_K_IDLE_X100` SHALL have a comment: `@deprecated Legacy variance-based IDLE detection. Replaced by CONFIG_MONITOR_DSP_TKEO_HIGH_X10.`
- **THEN** `CONFIG_MONITOR_K_DISTURBED_X100` and `CONFIG_MONITOR_ABS_MIN_VAR_X10000` SHALL have similar comments

#### Scenario: Generic filter files left untouched
- **WHEN** a developer opens `madgwick_filter.hpp` or any file under `components/filter/` except `adaptive_complementary_filter.hpp` and `filter_math.hpp`
- **THEN** the file SHALL NOT receive any documentation or @deprecated tags
- **THEN** these files are considered generic DSP building blocks, not project-specific artifacts

### Requirement: Deprecated method declarations SHALL reference their replacements
Every `@deprecated` tag on a method or type SHALL name the replacement API so developers can find the active implementation.

#### Scenario: Deprecated tag includes replacement name
- **WHEN** a developer reads a @deprecated tag on `FindDecayRegion`
- **THEN** the text SHALL include "AnalyzeImuEvent" as the replacement
- **THEN** the text SHALL include "FindDecayOnsetTkeo" as the replacement for decay onset logic
