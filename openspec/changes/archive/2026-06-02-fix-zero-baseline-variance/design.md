## Context

The monitor component uses a two-state machine (IDLE / DISTURBED) with transitions driven by comparing short-buffer rolling variance against a 5-minute baseline variance. The baseline is recomputed each time the storage buffer wraps while in IDLE (see [monitor.cpp L236-263](file:///C:/Users/avila/Documents/projects/ranting-jatuh-node/components/monitor/monitor.cpp#L236-L263)). Transitions are checked every sample in [PushSample()](file:///C:/Users/avila/Documents/projects/ranting-jatuh-node/components/monitor/monitor.cpp#L315-L382).

When the device is perfectly still, baseline variance drops to ~1e-7 (pure ADC/sensor noise). The relative multiplier thresholds (`K_IDLE=1.5x`, `K_DISTURBED=1.1x`) become meaningless at this scale — any sample-to-sample noise exceeds them.

## Goals / Non-Goals

**Goals:**
- Eliminate false IDLE→DISTURBED transitions caused by near-zero baseline variance
- Eliminate stuck DISTURBED state caused by impossibly low return-to-IDLE threshold
- Introduce a configurable absolute variance floor that represents the sensor noise level
- Zero impact on behavior when baseline variance is above the floor (normal operation)

**Non-Goals:**
- Changing the fundamental state machine architecture (two-state model stays)
- Adaptive/auto-calibrated noise floor detection (static config value is sufficient for now)
- Modifying FFT, sway, damping, or any other computation logic
- Power optimization or deep-sleep integration

## Decisions

### Decision 1: Hybrid threshold using `max(relative, absolute)`

**Choice:** Effective threshold = `max(baseline * K_multiplier, K_ABS_MIN_VAR)`

**Alternatives considered:**
- *Variance floor on baseline only* — clamp `idle_5min_*_var_` to minimum. Simpler but hides real variance from published `MonitorResult`, which server may want for anomaly detection.
- *EMA baseline smoothing* — prevents sudden drops but eventually converges to zero anyway. Doesn't solve root cause.
- *Debounce counter* — adds transition latency for all cases, not just the zero-baseline edge case.

**Rationale:** Hybrid preserves raw baseline in `MonitorResult` for server analysis. Floor only activates when baseline is meaninglessly small. Clean separation of concerns — no impact on published data, only on transition logic.

### Decision 2: Kconfig scaled integer for absolute threshold

**Choice:** `MONITOR_ABS_MIN_VAR_X10000` (integer, default 50 → 0.005 deg²)

**Rationale:** ESP-IDF Kconfig doesn't support float. Scale factor x10000 gives 4 decimal places of precision — sufficient for variance values in the 0.001-0.01 range typical of sensor noise. Consistent with existing pattern (`K_IDLE_X100`, `K_DISTURBED_X100`).

### Decision 3: Apply floor in PushSample() transition checks, not in baseline storage

**Choice:** Keep `idle_5min_roll_var_` and `idle_5min_pitch_var_` as raw computed values. Apply floor only at the comparison site.

**Rationale:** Raw baseline values in `MonitorResult` events give the server true sensor behavior data. The floor is purely a local decision-making guard, not a data correction.

## Risks / Trade-offs

- **Wrong default floor value** → Might still be too low or too high for specific sensor units. Mitigation: configurable via Kconfig; document how to measure noise floor (leave device still, observe variance log output).
- **Per-axis vs combined floor** → Using same floor for both roll and pitch. If sensor has asymmetric noise, one axis might still trigger. Mitigation: acceptable for v1; can split into per-axis params later if needed.
- **Floor hides real micro-vibrations** → If branch has very subtle constant vibration below the floor, transitions won't fire. Mitigation: floor should be set at sensor noise level, not environmental noise level. Real vibrations should be above this.
