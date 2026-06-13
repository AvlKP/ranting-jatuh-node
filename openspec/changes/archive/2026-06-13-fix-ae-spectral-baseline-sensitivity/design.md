## Context

The AE spectral detector was ported from an Arduino prototype. Three parameter deviations cause the gradient danger detector to fire on the first full FFT window and never recover:

1. Initial EWMA variance is 0.01 (sigma=0.1) vs Arduino's 100.0 (sigma=10.0) — a 1000x difference in variance, 100x in sigma.
2. EWMA variance update formula uses `var = (1-alpha) * (var + alpha * diff^2)` while Arduino uses `var = alpha * diff^2 + (1-alpha) * var`.
3. Gradient is not clamped to >=0; Arduino uses `if (gradien_kenaikan < 0) gradien_kenaikan = 0;`.

The current code in `components/monitor/monitor.cpp:AeSpectralDetector` methods `Reset()` and `UpdateEnergy()` contains all three deviations.

## Goals / Non-Goals

**Goals:**
- Match Arduino prototype baseline initialization (variance=100.0, sigma=10.0).
- Match Arduino EWMA variance update formula.
- Clamp gradient to >=0 before danger threshold comparison and baseline adaptation.
- Disable failure event publishing from energy-jump latch (`peringatan_retak`). Only adaptive gradient danger (`status_patah`) publishes `FailureEvent::AcousticEmission`.

**Non-Goals:**
- No Kconfig parameter changes (thresholds, alphas, window sizes stay as-is).
- No removal of latch state tracking — latch still tracks internally, just does not publish.
- No ADC or FFT path changes.
- No new failure event types or GPIO outputs.

## Decisions

### Fix variance initialization in `Reset()` instead of adding Kconfig

Change `ewma_variance_{0.01f}` → `ewma_variance_{100.0f}` and `sigma_{0.1f}` → `sigma_{10.0f}` in `AeSpectralDetector::Reset()`. No new Kconfig key needed — the initial values should always match Arduino, and the baseline adapts automatically from there.

Alternative considered: add `CONFIG_MONITOR_AE_SPECTRAL_INITIAL_SIGMA_X100` Kconfig. Rejected because initial sigma should not be tunable; the self-adapting baseline renders any initial value temporary. 10.0 matches the Arduino exactly and gives the detector the same ramp-up grace period.

### Use Arduino's direct EWMA formula for variance

Replace:
```cpp
ewma_variance_ = (1.0f - config.spectral_ewma_alpha) *
    (ewma_variance_ + (config.spectral_ewma_alpha * diff * diff));
```
with:
```cpp
ewma_variance_ = (config.spectral_ewma_alpha * diff * diff) +
    ((1.0f - config.spectral_ewma_alpha) * ewma_variance_);
```

The Arduino formula gives full weight `alpha` to the new squared difference. The current formula attenuates it by `alpha * (1-alpha)`, which with alpha=0.05 means 0.0475 vs 0.05 — a 5% difference. Minor but compounds over many windows.

### Clamp gradient to zero after computation, before all uses

Add `if (gradient < 0.0f) gradient = 0.0f;` after the gradient is computed from the ring buffer. This is a single-line addition that mirrors Arduino's `if (gradien_kenaikan < 0) gradien_kenaikan = 0;`.

## Risks / Trade-offs

- **Larger initial sigma delays first valid detection** — For the first 20 FFT windows (~128ms), danger detection is suppressed by the high initial sigma. This matches Arduino behavior. Acceptable because the detector self-calibrates to the noise floor within seconds.
- **Gradient clamp may mask real energy decreases** — If the integrator legitimately decreases (decaying signal), the zero clamp loses that information. However, the Arduino prototype clamps and the danger condition only triggers on rising integrator, so this matches intended behavior.
- **Unit test expected values shift** — Tests that assert specific sigma/threshold values after Reset() or early UpdateEnergy() calls will need updated expected values.

### Suppress latch from failure event publish gate

Remove `latch_started` from `should_publish` and change the interval-due repeat-publish gate to only consider `danger_active_` instead of the combined `latch_active_ || danger_active_`. The latch state variables (`latch_active_`, `latch_until_ms_`) and result fields (`latch_active`, `latch_started`) remain populated for diagnostics and dashboard visibility.

The `should_publish` expression changes from:
```cpp
result.should_publish = result.latch_started || result.danger_started || (was_active && interval_due);
// was_active = was_latch_active || was_danger_active
```
to:
```cpp
const bool was_danger = was_danger_active;
const bool interval_due = danger_active_ && ...;
result.should_publish = result.danger_started || (was_danger && interval_due);
```

Alternative considered: remove latch entirely. Rejected because latch state is useful for future AE characterization and does not add meaningful cost.

## Open Questions

None — the four fixes are straightforward parameter and gate corrections.
