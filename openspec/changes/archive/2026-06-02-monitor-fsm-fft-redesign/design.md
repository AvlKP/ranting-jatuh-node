## Context

The monitor component runs a FreeRTOS task at 26 Hz on core 1, reading an LSM6DS3 IMU over I2C. Raw accelerometer and gyroscope data flow through a complementary filter to produce roll and pitch estimates. A 2-state FSM (IDLE/DISTURBED) driven by tilt variance governs when to compute natural frequency (FFT) and damping ratio (log-decrement).

Two problems were discovered during testing:
1. **FFT on `√(roll²+pitch²)` acts as full-wave rectification**, producing dominant DC/near-DC peaks and frequency doubling. A 4 Hz oscillation shows up as ~0 Hz.
2. **Tilt variance cannot distinguish forced oscillation from free decay** — both show high tilt amplitude. The system conflates forced-response data with free-decay data in a single buffer and computation pass.

The accelerometer magnitude `√(ax²+ay²+az²)` is always ~1g at rest regardless of orientation, and deviates during dynamic motion. This makes `|accel_mag - 1g|` a direct, orientation-independent proxy for external forcing — distinct from tilt, which remains elevated during free decay.

## Goals / Non-Goals

**Goals:**
- Fix the FFT to correctly identify oscillation frequency by computing per-axis FFT on roll and pitch
- Use accelerometer error variance for state transitions to detect forcing onset/cessation
- Add FREE_DECAY state to isolate post-forcing data for natural frequency and damping computation
- Keep the same buffer infrastructure (compile-time `std::array`, rolling variance) and task architecture
- Maintain backward-compatible MQTT interface (additive field changes only)

**Non-Goals:**
- Operational Modal Analysis or frequency-domain decomposition techniques
- Deep-sleep or low-power mode changes
- Changes to the complementary filter, EKF, or sensor driver
- Server-side analysis algorithm changes
- Minimum FFT window policy (deferred)
- Free-decay segment detection within buffer (deferred — use state transition as proxy)

## Decisions

### Decision 1: Per-axis FFT instead of magnitude FFT

**Choice:** Run FFT separately on roll and pitch signals, report `natural_freq_roll_hz` and `natural_freq_pitch_hz`.

**Alternatives considered:**
- *Combined PSD (FFT per axis, sum PSDs, single peak)*: simpler output but loses axis information. Tree branches may have different stiffness in different planes.
- *FFT on magnitude with high-pass filter*: removes DC bias but not frequency doubling. Fundamentally broken for sinusoidal inputs.
- *Notch filter at DC*: same problem — can't fix rectification artifacts.

**Rationale:** Per-axis signals are zero-mean after taring, so a 4 Hz sway produces a clean 4 Hz peak. No rectification artifacts. Axis separation also allows detecting asymmetric branch behavior (different natural frequencies in roll vs pitch planes).

### Decision 2: Accelerometer error variance for state transitions

**Choice:** Compute `accel_err = |√(ax²+ay²+az²) - 1.0f|` from raw IMU output (g units) each sample. Maintain a rolling variance in a short buffer (256 samples, ~9.8s). Use this variance against a baseline (5-min IDLE accumulation) for all state transitions.

**Alternatives considered:**
- *Keep tilt variance*: doesn't distinguish forced from free decay.
- *Gyroscope-based metric*: also orientation-dependent and subject to drift.
- *High-frequency accelerometer energy (bandpass)*: more complex, requires FFT or IIR filter per sample.

**Rationale:** Accel magnitude error is orientation-independent, responds instantly to forcing changes (no filter lag), and naturally separates forcing (high variance) from free decay (moderate, decreasing) from rest (noise floor). Reuses the existing rolling sum/sum-of-squares pattern.

### Decision 3: Three-state FSM with threshold hysteresis

**Choice:**
```
IDLE ──[accel_err_var > k_high × baseline]──▶ DISTURBED
DISTURBED ──[accel_err_var < k_mid × baseline for 128 samples]──▶ FREE_DECAY
FREE_DECAY ──[accel_err_var < k_low × baseline]──▶ IDLE
FREE_DECAY ──[accel_err_var > k_high × baseline]──▶ DISTURBED (re-excitation)
FREE_DECAY ──[timeout]──▶ IDLE
```

Three thresholds (`k_high`, `k_mid`, `k_low`) create hysteresis bands. The 128-sample debounce on DISTURBED→FREE_DECAY prevents false transitions during wind lulls.

**Alternatives considered:**
- *Variance rate-of-change detection*: more robust but requires tracking variance history and computing derivative. Added complexity.
- *Spectral narrowband detection*: distinguish broadband (forced) from narrowband (decay) in accel. Too expensive per-sample.

**Rationale:** Threshold + debounce is simple, deterministic, O(1) per sample, and consistent with the existing design pattern. The three thresholds provide enough degrees of freedom to tune for real-world conditions.

### Decision 4: Buffer management per state

**Choice:**
- **IDLE**: roll/pitch accumulate in 5-min circular buffer for baseline stats. Accel error accumulates for baseline variance.
- **DISTURBED**: reset disturbance buffer at entry (pre-roll from short buffer). Accumulate for sway stats.
- **FREE_DECAY**: continue the *same* buffer for sway (spanning both states). Mark the decay start index for FFT/damping computation.
- On DISTURBED→FREE_DECAY: publish sway stats computed so far (they continue accumulating). Record `decay_start_index_`.
- On FREE_DECAY→IDLE: compute FFT and damping on `[decay_start_index_, write_index_)` range only. Publish combined sway + freq + damping result.

**Alternatives considered:**
- *Separate buffers per state*: wastes RAM (ESP32-S3 has 512KB, but arrays are already large at 5 min × 26 Hz = 7800 floats × 2 axes).
- *Reset buffer at FREE_DECAY entry*: loses DISTURBED sway data.

**Rationale:** Reusing the same buffer with a start-index marker avoids RAM duplication. Sway spans both phases (user decision). FFT/damping uses only the decay portion.

### Decision 5: MonitorResult struct changes

**Choice:** Add fields to existing struct:
```cpp
struct MonitorResult {
    // ... existing fields ...
    float natural_freq_roll_hz{0.0f};   // NEW: replaces natural_freq_hz
    float natural_freq_pitch_hz{0.0f};  // NEW
    NodeState state{NodeState::IDLE};    // NEW
    // Keep natural_freq_hz for backward compat, set to max(roll, pitch)
};
```

**Rationale:** Additive changes preserve backward compatibility. Logger and dashboard can be updated incrementally.

## Risks / Trade-offs

**[Risk] Accel error during large-amplitude free decay may still be significant** → Centripetal acceleration from swaying can cause measurable `|accel_mag - 1g|`. Mitigation: k_mid tuning. The debounce period helps — 128 samples means the variance is averaged over ~5 seconds, smoothing out individual large-amplitude swings.

**[Risk] Three thresholds (k_high, k_mid, k_low) require more tuning** → More configuration knobs. Mitigation: start with reasonable defaults (k_high=1.5, k_mid=1.2, k_low=1.1 relative to baseline), expose via Kconfig with the existing x100 integer pattern.

**[Risk] SHORT_BUFFER_DEBOUNCE may delay valid DISTURBED→FREE_DECAY transition** → 128 samples = 4.9 seconds. If a short wind gust stops and restarts within 5 seconds, the system stays in DISTURBED (correct behavior). But if free decay is genuinely short (<5s), it may miss the decay window. Mitigation: configurable, can be reduced for stiff/high-frequency branches.

**[Risk] Per-axis FFT doubles FFT computation cost** → Two FFTs instead of one per decay event. Mitigation: FFT only runs at state transitions (not per-sample), and ESP32-S3 has hardware-accelerated DSP. At ~39ms per 1024-point FFT, two FFTs add <100ms total — negligible.

**[Trade-off] Sway spanning DISTURBED+FREE_DECAY** → Sway amplitude includes decay portion where amplitude is decreasing. This inflates pp_mean slightly but pp_max (which is the primary risk indicator) is unaffected since maximum sway occurs during forced oscillation. Accepted trade-off per user decision.
