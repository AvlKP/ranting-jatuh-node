## Why

The project lacks consistent documentation across its Python reference implementation and C++ firmware components. Algorithm files, public APIs, data structures, and module interfaces have no docstrings or Doxygen comments. This slows onboarding, makes code review harder, and obscures the algorithmic intent that was surfaced during the Python-vs-C++ comparison analysis. Adding docstrings now — while the codebase is still under active development — prevents documentation debt from accumulating further.

## What Changes

- Add **Doxygen-style docstrings** to all C++ public headers and implementation files across `components/monitor/`, `components/logger/`, `components/lsm6ds3/`, `components/dashboard/`, and `main/`.
- Add **@deprecated Doxygen tags** to vestigial project-specific code (ChebyshevHpf, old monitor modal analysis methods, dead class members, dead Kconfig keys) to signal superseded design paths without code removal.
- Add **Python docstrings** (Google-style) to `imu_algorithms/` modules, classes, and public functions, including Python-only reference functions intentionally not ported to C++.
- Add **per-file brief** comments explaining module purpose at the top of each source file.
- Add **algorithm derivation notes** for critical math (TKEO, adaptive complementary filter, peak-hold envelope, OLS damping regression) referencing the Python reference.

## Capabilities

### New Capabilities
- `doxygen-documentation`: Doxygen-style docstrings for all C++ public headers, covering classes, methods, enums, structs, and constants. Algorithm derivation notes for signal processing functions. @deprecated tags on vestigial project-specific code.
- `python-reference-docstrings`: Google-style docstrings for the Python reference implementation in `imu_algorithms/`, covering public API, pipeline, and algorithm functions. Includes full documentation for Python-only functions intentionally not ported to C++.

### Modified Capabilities
- `chebyshev-hpf-disturbance`: The existing spec requirements are marked REMOVED with reason and migration notes, documenting the transition to TKEO-based DSP detection. The code itself is tagged @deprecated but not deleted (separate removal proposal will handle deletion).

## Impact

- Affected code: All `.hpp`, `.cpp`, `.py` files under `components/`, `main/`, and `imu_algorithms/`.
- Files with @deprecated tags added: `chebyshev_hpf.hpp`, `monitor.hpp`, `monitor.cpp`, `components/monitor/Kconfig`.
- Files explicitly left untouched: `components/filter/` (generic DSP building blocks, not project-specific).
- No files deleted. No methods or structs removed. No Kconfig keys removed.
- No API or MQTT schema changes.
- No build changes.
