## Why

The natural frequency calculation pipeline is the most complex signal-processing chain in the firmware — spanning TKEO decay detection, dominant axis selection, FFT with configurable search bands, and OLS log-fit damping regression. There is no single document that explains how these pieces fit together. Developers debugging frequency/damping results must reverse-engineer the pipeline by reading 300+ lines of C++ across `monitor.cpp` functions. A visual, structured reference document would dramatically reduce onboarding time and debugging friction.

## What Changes

- Create `docs/natural-frequency-pipeline.md`: A deep-dive reference documenting the full natural frequency + damping calculation pipeline
- Include mermaid diagrams showing data flow, function call graphs, and the mathematical pipeline
- Document the FFT parameters (window size, bin resolution, search band) and their Kconfig origins
- Document the comparison between C++ firmware implementation and Python reference (`imu_algorithms/_extraction.py`)
- Cross-reference all related source files, Kconfig options, and OpenSpec specs

## Capabilities

### New Capabilities
<!-- This is a documentation-only change. No new functional capabilities. -->
None.

### Modified Capabilities
None.

## Impact

- Affected files: New file `docs/natural-frequency-pipeline.md`
- No code changes, no API changes, no dependency changes
- References existing source: `components/monitor/monitor.cpp`, `components/monitor/include/monitor.hpp`, `components/monitor/Kconfig`, `imu_algorithms/_extraction.py`
- References existing specs: `free-decay-analysis`, `imu-event-analysis`, `envelope-damping-regression`, `noise-gate`, `node-state-machine`
