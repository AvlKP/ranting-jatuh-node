## Why

Current centerline modal analysis treats each local maximum/minimum as a candidate extrema. Noisy pull-and-release data can contain multiple local maxima inside one physical peak lobe, or multiple local minima inside one trough lobe, which can create wrong peak/trough pairs and jagged damping amplitudes.

## What Changes

- Add notebook-only lobe/half-cycle extrema collapse before centerline pairing.
- Choose the strongest maximum within a peak lobe before the next trough lobe.
- Choose the strongest minimum within a trough lobe before the next peak lobe.
- Use collapsed extrema for centerline interpolation, half peak-to-peak damping amplitudes, and baseline-compensation diagnostics.
- Preserve raw extrema diagnostics for visibility and comparison.
- Keep production firmware out of scope.

## Capabilities

### New Capabilities

### Modified Capabilities
- `notebook-centerline-modal-analysis`: Centerline and damping envelope SHALL use lobe-collapsed extrema rather than every adjacent local extrema.

## Impact

- Affects `notebook/natural_frequency.py` and `notebook/analysis.ipynb`.
- May update notebook notes if the implementation confirms useful tuning values.
- No production ESP32-S3 firmware, MQTT schema, logger, server, or stored data format changes.
