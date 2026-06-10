# python-reference-docstrings Specification

## Purpose
Google-style docstrings for all public API surfaces, classes, functions, and modules in the Python reference implementation under `imu_algorithms/`. Includes full documentation for Python-only reference functions intentionally not ported to C++, which serve as algorithmic reference for future porting.

## ADDED Requirements

### Requirement: Every module SHALL have a module-level docstring
Every `.py` file in `imu_algorithms/` SHALL begin with a module-level docstring describing the module's purpose, the algorithms it implements, and usage notes for ESP32 target deployment.

#### Scenario: Module with docstring
- **WHEN** a developer opens `imu_algorithms/_detection.py`
- **THEN** the first statement after imports SHALL be a multi-line docstring
- **THEN** it SHALL describe TKEO computation and the Schmitt trigger state machine
- **THEN** it SHALL note ESP32 feasibility (O(1) per sample, fixed state, no allocation)

#### Scenario: Package init with docstring
- **WHEN** a developer opens `imu_algorithms/__init__.py`
- **THEN** it SHALL document the package purpose and provide a usage example
- **THEN** the usage example SHALL show `Pipeline` and `run_pipeline` imports

### Requirement: Every public class SHALL have a class docstring with attribute documentation
Every public class SHALL have a docstring describing its purpose, responsibility, and key attributes. Dataclass attributes SHALL be documented inline or in the class docstring.

#### Scenario: EventDetector class documentation
- **WHEN** a developer reads `EventDetector`
- **THEN** the docstring SHALL describe the Schmitt-trigger state machine
- **THEN** it SHALL document the two-state design (IDLE, ACTIVE)
- **THEN** constructor parameters (hi_thresh, lo_thresh, etc.) SHALL document their default values and scaling

#### Scenario: EventResult dataclass documentation
- **WHEN** a developer reads `EventResult`
- **THEN** each field SHALL have type annotations
- **THEN** fields with non-obvious semantics (e.g., `offset_write_ptr`, `is_truncated`) SHALL have inline comments
- **THEN** units SHALL be documented (Hz, degrees, seconds)

### Requirement: Every public function SHALL document parameters, returns, and algorithm
Every public function SHALL use Google-style docstrings with `Args:`, `Returns:`, and optional `Raises:` sections. Algorithm functions SHALL describe the method in prose before the structured sections.

#### Scenario: Function with algorithm description
- **WHEN** a developer reads `envelope_peak_hold`
- **THEN** the docstring SHALL describe the asymmetric peak-hold detector
- **THEN** it SHALL document `Args: gmag`, `dt`, `fc`
- **THEN** it SHALL document `Returns:` the envelope array
- **THEN** it SHALL note ESP32 feasibility (O(N) single-pass, one multiply-add per sample)

#### Scenario: Function with return tuple documentation
- **WHEN** a developer reads `find_decay_onset_tkeo`
- **THEN** the docstring SHALL document the return tuple structure `(decay_onset, quality)`
- **THEN** each return component SHALL have its type and meaning documented

### Requirement: Private helper functions SHALL have concise docstrings
Functions prefixed with `_` in module files SHALL have brief docstrings explaining their purpose and algorithm, though less formal than public function docstrings.

#### Scenario: Private helper documented
- **WHEN** a developer reads `_tkeo_pos`
- **THEN** it SHALL have a docstring explaining it computes non-negative TKEO energy
- **THEN** it SHALL document boundary handling (buf[0] = buf[-1] = 0)

### Requirement: Python-only reference functions SHALL be fully documented
Functions intentionally not ported to C++ (e.g., `classify_event`, `is_dynamic`, `extract_active_region`, `extract_frequency_zc`, `extract_frequency_pk`) SHALL receive full Google-style docstrings as reference implementations. They SHALL NOT be marked deprecated. The docstrings SHALL note if the function provides algorithmic knowledge useful for future C++ porting.

#### Scenario: Python-only function documented as reference
- **WHEN** a developer reads `extract_frequency_zc`
- **THEN** the docstring SHALL describe the zero-crossing method for natural frequency estimation
- **THEN** it SHALL note it is the cheapest method (O(n), no FFT) and recommended for future ESP32 porting
- **THEN** it SHALL NOT have any @deprecated tag

#### Scenario: Event classifier documented as reference
- **WHEN** a developer reads `classify_event`
- **THEN** the docstring SHALL describe the decision tree (pull_release, oscillation, flick, pull_hold)
- **THEN** it SHALL note this classification is Python-only and intentionally not in C++ (all disturbances treated uniformly)
- **THEN** it SHALL document the threshold values for each classification
