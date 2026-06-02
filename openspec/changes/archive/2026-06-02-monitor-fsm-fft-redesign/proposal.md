## Why

The current monitor FFT computes frequency on `√(roll²+pitch²)`, which acts as a full-wave rectifier — a 4 Hz oscillation appears as a near-0 Hz dominant peak due to DC bias and frequency doubling. The 2-state FSM (IDLE/DISTURBED) uses tilt variance for transitions, but tilt remains high during free decay, preventing the system from distinguishing forced oscillation from natural frequency response. Natural frequency and damping ratio calculations require free-decay data, which the current design cannot isolate.

## What Changes

- **Replace magnitude-based FFT with per-axis FFT**: compute FFT on roll and pitch signals independently, producing `natural_freq_roll_hz` and `natural_freq_pitch_hz`. Eliminates rectification artifacts and DC bias.
- **Switch state transition metric from tilt variance to accelerometer-error variance**: use `|√(ax²+ay²+az²) - 1g|` variance for IDLE/DISTURBED detection. Responds immediately to external forcing changes, unlike tilt which lags through the complementary filter.
- **Add FREE_DECAY state**: new intermediate state between DISTURBED and IDLE. Captures the post-forcing decay window for natural frequency (FFT) and damping ratio (log-decrement) computation.
- **Separate computation domains per state**: IDLE publishes baseline tilt stats, DISTURBED publishes sway amplitude, FREE_DECAY publishes natural frequency and damping ratio.
- **Add `state` field to MonitorResult**: MQTT messages include the originating state so the server can interpret parameters in context.
- **Sway statistics span DISTURBED + FREE_DECAY**: sway `pp_max`/`pp_mean` accumulate across both forced and decay phases.
- **DISTURBED→FREE_DECAY debounce**: 128 samples (~4.9s at 26 Hz) below threshold before transition.

## Capabilities

### New Capabilities
- `accel-error-state-detection`: accelerometer magnitude error variance tracking for state transition decisions, replacing tilt-based transitions
- `free-decay-analysis`: FREE_DECAY state with dedicated buffer for natural frequency and damping ratio computation from post-disturbance oscillation data

### Modified Capabilities
- `node-state-machine`: transition logic changes from tilt variance to accel-error variance; adds FREE_DECAY state with transition rules and debounce
- `logger-fsm-adaptation`: logger must handle the new FREE_DECAY state and per-axis frequency fields
- `dashboard-fsm-adaptation`: dashboard must render FREE_DECAY state and per-axis natural frequency values

## Impact

- **monitor component**: major refactor of `ComputeNaturalFrequency()` (per-axis FFT), `PushSample()` (accel-error tracking + 3-state FSM), `ComputeAndPublish()` (state-specific publication), `MonitorResult` struct (new fields)
- **monitor Kconfig**: new parameters for accel-error thresholds (`K_MID`), debounce count, FREE_DECAY timeout
- **logger component**: must format and transmit new fields (`natural_freq_roll_hz`, `natural_freq_pitch_hz`, `state`)
- **dashboard component**: must display per-axis frequencies and FREE_DECAY state indicator
- **MQTT interface**: message payload schema changes (new fields, modified semantics)
- **No new hardware or library dependencies**
