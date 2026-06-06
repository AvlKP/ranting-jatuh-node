## Why

Natural frequency and damping ratio calculations produce incorrect results: physical branch oscillation is >1 Hz but firmware consistently reports ~0.051 Hz (bin 1 of 512-pt FFT). Damping ratio is also wrong because it depends on the incorrect natural frequency. There is no way to inspect intermediate computation data (decay region bounds, raw FFT input, peak detection output, regression parameters) to diagnose whether the bug is in decay region selection, FFT windowing, peak detection, or the regression itself. A debug dump of all intermediate data enables both manual visualization (matplotlib) and automated AI agent analysis.

## What Changes

- Add an optional debug data dump to SD card triggered on each DISTURBED→IDLE transition.
- Dump contains: full DISTURBED buffer (roll/pitch), decay region metadata, peak list (amplitudes + times), and ESP32-computed results (frequency, damping ratio).
- Output format is tagged CSV, parseable by a companion Python script.
- Add a Python analysis script that parses the dump, recomputes FFT/damping independently with scipy, compares against ESP32 results, and generates diagnostic plots.
- Feature gated behind `CONFIG_MONITOR_DEBUG_DUMP` (Kconfig, default off).

## Capabilities

### New Capabilities
- `debug-dump-sd`: SD card debug data dump of intermediate computation state on DISTURBED→IDLE transitions, with companion Python analysis script.

### Modified Capabilities
(none — this is purely additive debug instrumentation, no existing behavior changes)

## Impact

- **Code**: `monitor.cpp` — add ~50 lines for `DumpDebugToSD()`, called from `ComputeAndPublish()` when `is_exit && CONFIG_MONITOR_DEBUG_DUMP`.
- **Config**: `Kconfig` — add 1 bool entry.
- **Header**: `monitor.hpp` — add 1 private method declaration.
- **New file**: `scripts/debug_analyze.py` — Python analysis/visualization script (~200 lines).
- **No changes** to logger, dashboard, MQTT, or any existing behavior.
