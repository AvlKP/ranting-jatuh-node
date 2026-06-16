## Context

The firmware computes natural frequency and damping ratio on DISTURBED-to-IDLE transitions via `Monitor::AnalyzeImuEvent()` (monitor.cpp:1501). The pipeline is non-trivial: TKEO-based decay onset detection, dominant-axis selection via integrated sway, FFT on signed gyro data within a configurable search band, and OLS log-fit damping regression. There is no visual reference tying these stages together. This change creates a single markdown document at `docs/natural-frequency-pipeline.md` with mermaid diagrams, parameter tables, and cross-references.

Existing documentation:
- `ARCHITECTURE.md` line 219 references `ComputeSignedAxisNaturalFrequency()` but shows only a table entry, no pipeline detail
- `imu_algorithms/_extraction.py` has per-function docstrings and is the Python reference implementation
- `components/monitor/monitor.cpp` contains the actual C++ implementation with inline algorithm comments
- `openspec/specs/free-decay-analysis/`, `imu-event-analysis/`, `envelope-damping-regression/`, `noise-gate/` define the requirements

## Goals / Non-Goals

**Goals:**
- Document the complete natural frequency + damping pipeline from FSM trigger to output fields
- Include mermaid diagrams: data flow (sequence), FFT pipeline (flowchart), function call hierarchy
- Provide a parameter reference table mapping Kconfig options to algorithm behavior (search band, FFT size, sample rate)
- Document the C++ vs Python reference comparison (key algorithmic differences)
- Cross-reference source files (line numbers), Kconfig keys, and OpenSpec specs

**Non-Goals:**
- Modify any source code or Kconfig defaults
- Add new functional capabilities or change existing behavior
- Port Python algorithms (zero-crossing, peak-to-peak) to C++
- Create similar docs for AE spectral pipeline, FSM state machine, or logger

## Decisions

1. **Document location**: `docs/natural-frequency-pipeline.md` — follows existing convention of `ARCHITECTURE.md` and `mqtt_interface.md` at repo root
2. **Diagram format**: Mermaid with `sequenceDiagram` for the FSM-triggered flow and `flowchart LR` for the FFT internal pipeline
3. **Document structure**: Top-down — FSM trigger → pipeline stages → FFT internals → damping regression → output fields → Python comparison
4. **Parameter table**: All configurable values referenced with their Kconfig key path and default value in a single reference table
5. **No separate spec file**: This change has no functional requirements (no code changes). The proposal lists zero capabilities. Skipping spec creation per schema.

## Risks / Trade-offs

- [Risk] Document drifts out of date as pipeline evolves → Mitigation: Cross-reference line numbers in source; include "last verified" date; add to the scope of any future pipeline changes
