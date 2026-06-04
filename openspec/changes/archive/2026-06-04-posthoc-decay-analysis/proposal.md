## Why

The FREE_DECAY state is non-functional: the DISTURBED→FREE_DECAY debounce (128 samples, ~4.9s at 26Hz) consumes the entire physical decay before the state is entered. By the time FREE_DECAY starts, `accel_err_var` is already near-zero, causing immediate exit to IDLE (zero debounce on exit). Natural frequency and damping ratio are never meaningfully computed.

Additionally, `PEAK_MIN_SPACING=5` limits peak detection to ≤2.6Hz at 26Hz ODR — too low for bench-scale branch samples (3–12Hz). `PEAK_MIN_AMPLITUDE=2°` is too high for late-decay peaks. The damping algorithm uses only 3 peaks (noisy for lightly-damped systems).

## What Changes

- **BREAKING**: Remove `FREE_DECAY` state from `NodeState` enum. FSM becomes two-state: `IDLE ↔ DISTURBED`.
- Replace real-time decay detection with **post-hoc retroactive analysis** on the full DISTURBED buffer when returning to IDLE.
- Post-hoc analysis retroactively identifies the decay region by finding the peak amplitude and tracking the declining envelope.
- Replace 3-peak log decrement damping with **linear regression on log-amplitude envelope** using all available peaks for robustness.
- Tune peak detection defaults: `PEAK_MIN_SPACING` 5→2, `PEAK_MIN_AMPLITUDE` 2°→0.5°.
- Add debounce to DISTURBED→IDLE exit transition.
- Remove FREE_DECAY-specific Kconfig: `FREE_DECAY_DEBOUNCE`, `FREE_DECAY_TIMEOUT_S`, `K_MID`.

## Capabilities

### New Capabilities
- `posthoc-decay-detection`: Retroactive identification of the decay region within the stored DISTURBED buffer by tracking the peak amplitude envelope.
- `envelope-damping-regression`: Robust damping ratio estimation via least-squares linear regression on log(peak amplitudes) vs time, replacing the 3-peak log decrement method.

### Modified Capabilities
- `node-state-machine`: Remove `FREE_DECAY` state. Two-state FSM (IDLE ↔ DISTURBED). DISTURBED→IDLE exit gets debounce. On DISTURBED→IDLE, trigger post-hoc analysis instead of relying on FREE_DECAY data.
- `free-decay-analysis`: FFT and damping are now computed post-hoc on retroactively identified decay segments from the DISTURBED buffer, not from a separately-captured FREE_DECAY buffer.
- `accel-error-state-detection`: Remove `K_MID` threshold (no longer needed without FREE_DECAY). Keep `K_HIGH` (entry) and `K_LOW` (exit with debounce).

## Impact

- **Code**: `monitor.cpp` FSM logic, `monitor.hpp` enum, `ComputeSwayAndDamping()`, `ComputeNaturalFrequency()`.
- **Config**: `Kconfig` — remove 3 entries (`FREE_DECAY_DEBOUNCE`, `FREE_DECAY_TIMEOUT_S`, `K_MID`), add 1 (`DISTURBED_EXIT_DEBOUNCE`), change 2 defaults (`PEAK_MIN_SPACING`, `PEAK_MIN_AMPLITUDE`).
- **Logger**: Must handle absence of `FREE_DECAY` state in CSV output. Logger FSM adaptation may need update.
- **Dashboard**: State display must not reference `FREE_DECAY`.
- **Specs**: 3 existing specs modified, 2 new specs.
