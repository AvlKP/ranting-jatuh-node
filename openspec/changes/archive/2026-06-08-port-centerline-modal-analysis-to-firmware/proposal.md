## Why

Notebook pull-and-release analysis showed that slow tilt baseline relaxation can bias firmware modal metrics when FFT and damping operate on raw tilt around a fixed zero axis. Firmware should use the same baseline-robust architecture before relying on `natural_freq_*` and `damping_ratio_*` for warnings.

## What Changes

- Port centerline modal analysis into the ESP-IDF monitor path for post-hoc DISTURBED buffers.
- Detect raw local extrema, collapse same-lobe maxima/minima, and build adjacent peak/trough pairs.
- Estimate a piecewise-linear centerline from pair midpoints and compute residual roll/pitch tilt.
- Compute natural frequency from centerline-compensated residuals inside a bounded frequency band.
- Compute damping from half peak-to-peak pair amplitudes instead of absolute distance from zero.
- Add Kconfig parameters for centerline pair amplitude, lobe reversal, and FFT search band.
- Keep implementation deterministic: fixed buffers, no heap allocation in analysis, no exceptions, O(N) extrema/centerline passes, bounded FFT work.
- Preserve existing MonitorResult fields and MQTT/storage payload shape unless a later change explicitly expands diagnostics.

## Capabilities

### New Capabilities

### Modified Capabilities
- `free-decay-analysis`: Firmware post-hoc modal analysis SHALL use centerline-compensated residuals for natural frequency and half peak-to-peak amplitudes for damping.
- `posthoc-decay-detection`: Firmware peak tracking SHALL support lobe-collapsed alternating extrema and expose enough pair data for centerline and damping estimation.

## Impact

- Affects `components/monitor/monitor.cpp`, `components/monitor/include/monitor.hpp`, `components/monitor/Kconfig`, and monitor unit tests.
- May affect logger/dashboard interpretation only through more stable values in existing frequency and damping fields.
- No production MQTT schema change expected.
- No dependency on nonlinear fitting, dynamic allocation, C++ exceptions, RTTI, or Python-only libraries.
- ESP32-S3 CPU and RAM usage increase from extra per-axis O(N) passes and temporary fixed-size analysis buffers.
