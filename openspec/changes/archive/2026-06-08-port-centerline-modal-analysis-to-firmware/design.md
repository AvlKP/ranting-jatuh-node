## Context

Firmware currently stores roll/pitch samples in fixed monitor history buffers during DISTURBED, identifies a decay region at DISTURBED->IDLE, computes per-axis FFT on raw tilt, and computes damping from absolute local-extrema amplitudes. That matches the earlier free-decay model, but it assumes the oscillation is centered near zero after startup tare.

Notebook analysis found a different shape in pull-and-release logs: oscillation continues while the branch baseline relaxes slowly. The notebook architecture now estimates a moving centerline from lobe-collapsed peak/trough pairs, subtracts that centerline before modal FFT, and uses half peak-to-peak amplitudes for damping. Firmware should port that architecture with ESP32-S3 constraints: deterministic execution, bounded memory, `esp_err_t`/boolean status style, C++ exceptions disabled, no heap allocation in analysis, no nonlinear optimizer, and no Python-only dependencies.

## Goals / Non-Goals

**Goals:**
- Make firmware `natural_freq_roll_hz`, `natural_freq_pitch_hz`, `roll_damping_ratio`, and `pitch_damping_ratio` robust to moving tilt baseline.
- Reuse existing DISTURBED buffer lifecycle and existing MonitorResult/MQTT fields.
- Add bounded FFT search defaults matching notebook modal analysis: 0.5 Hz to 12.0 Hz.
- Add lobe-collapsed extrema selection so multiple local maxima/minima inside one physical lobe do not create false pairs.
- Keep analysis memory bounded with compile-time fixed arrays sized from `kStorageSamples` and `PeakList::kMaxPeaks`.
- Keep the algorithm portable C++ inside `monitor`, with ESP-DSP only for FFT.

**Non-Goals:**
- No nonlinear exponential baseline fit.
- No dynamic allocation, `std::vector`, exceptions, RTTI, `std::function`, or virtual dispatch in the analysis path.
- No MQTT/storage schema expansion for centerline diagnostics in this change.
- No IMU ODR change, WiFi/MQTT behavior change, or logger/dashboard workflow change.
- No ADC continuous-mode or AE sensor redesign.

## Decisions

### D1: Add a firmware modal-analysis helper inside monitor

Create small fixed-data structs for raw extrema, collapsed extrema, centerline pairs, and modal-analysis output. Keep them private to `Monitor` or in a monitor-private header.

Candidate shape:

```text
ExtremaPoint { logical_index, value, kind }
CenterlinePair { center_logical_index_q1, center_value, amplitude, time_s }
ModalAxisResult { decay_region, pair_envelope, residual_count, natural_freq_hz, damping_ratio }
```

Rationale: monitor already owns buffers, physical/logical indexing, ESP-DSP FFT scratch, and debug dump behavior. Keeping this local avoids new component boundaries before algorithm stability is proven.

Alternative considered: separate `modal_analysis` component. Rejected for first firmware port because it adds CMake/API surface before current monitor integration points are stable.

### D2: Preserve raw history; compute residual into fixed scratch buffer

On DISTURBED->IDLE, keep `roll_history_` and `pitch_history_` unchanged. For each axis:

```text
history decay window
  -> raw extrema
  -> lobe-collapsed extrema
  -> adjacent opposite pairs
  -> interpolated centerline
  -> residual scratch
  -> bounded FFT on residual scratch
  -> damping from pair amplitudes
```

Use one `std::array<float, kStorageSamples>` residual scratch member and process roll then pitch sequentially, so RAM cost is one buffer, not two.

Rationale: preserving raw history keeps existing sway, debug, and future diagnostics usable. Sequential scratch reuse keeps RAM bounded.

Alternative considered: subtract centerline in-place from history. Rejected because it destroys raw samples needed for sway and debug dump.

### D3: Lobe collapse uses hysteresis-like reversal threshold

Raw local extrema detection remains a simple three-sample comparison with `CONFIG_MONITOR_PEAK_MIN_SPACING_SAMPLES`. The lobe collapse pass maintains one active representative extrema. Same-kind extrema replace the representative only if stronger. Opposite-kind extrema finalize the active lobe only when peak-to-trough difference reaches `centerline_lobe_reversal_deg`.

```text
same kind:
  peak: keep higher value
  trough: keep lower value
opposite kind:
  if abs(curr.value - active.value) >= reversal_threshold:
      emit active; start new lobe
  else:
      ignore as ripple
```

Rationale: this handles multiple local maxima/minima inside one physical peak/trough without nonlinear fitting or lookahead buffers. It is O(N), deterministic, and mirrors notebook intent.

Alternative considered: prominence search over sliding windows. Rejected because window choice interacts with frequency and ODR, and extra lookahead complicates streaming mental model.

### D4: Centerline is piecewise linear, not exponential

For adjacent opposite collapsed extrema above `centerline_min_amp_deg`:

```text
center_index = (left_index + right_index) / 2
center_value = (left_value + right_value) / 2
amplitude = abs(right_value - left_value) / 2
```

Interpolate linearly between center points. Before first center and after last center, hold the nearest center value. If fewer than two valid pairs exist, return zero frequency and zero damping for that axis, while existing sway metrics still publish.

Rationale: piecewise-linear centerline is deterministic and cheap. Exponential baseline fitting would require an optimizer or brittle fixed-form solver.

### D5: Bounded FFT search is bin-gated, not post-filtered

Add Kconfig values:

```text
CONFIG_MONITOR_MODAL_FREQ_MIN_HZ_X10 default 5   -> 0.5 Hz
CONFIG_MONITOR_MODAL_FREQ_MAX_HZ_X10 default 120 -> 12.0 Hz
```

Convert to integer bin bounds for each FFT size:

```text
min_bin = ceil(freq_min_hz * fft_size / sample_rate_hz)
max_bin = floor(freq_max_hz * fft_size / sample_rate_hz)
```

Clamp to valid non-DC bins and return 0.0 when no bin remains.

Rationale: low-frequency baseline residuals must not win natural-frequency selection. Integer bin bounds avoid repeated float comparisons in inner loops.

Alternative considered: high-pass filtering residual before FFT. Rejected for this change because centerline subtraction plus bin-gating addresses the observed issue without adding filter state.

### D6: Damping uses centerline pair envelope

Damping regression consumes half peak-to-peak amplitudes from centerline pairs, starting at the largest pair amplitude and continuing while amplitudes are non-increasing. It reuses current `ComputeDampingRegression` math with a pair-envelope list instead of absolute extrema from zero.

Rationale: half peak-to-peak amplitude does not require oscillation around y=0 and matches notebook architecture.

Alternative considered: use absolute residual extrema. Rejected because residual extrema can still be sensitive to centerline interpolation error and duplicated lobes.

### D7: Kconfig defaults separate sway thresholds from modal thresholds

Keep `CONFIG_MONITOR_PEAK_MIN_AMPLITUDE_X10` for existing sway/legacy peak detection. Add modal-specific parameters:

```text
CONFIG_MONITOR_CENTERLINE_MIN_AMPLITUDE_X100 default 5   -> 0.05 deg
CONFIG_MONITOR_CENTERLINE_LOBE_REVERSAL_X100 default 10  -> 0.10 deg
CONFIG_MONITOR_MODAL_FREQ_MIN_HZ_X10 default 5           -> 0.5 Hz
CONFIG_MONITOR_MODAL_FREQ_MAX_HZ_X10 default 120         -> 12.0 Hz
```

Rationale: late decay cycles useful for damping can be smaller than sway-warning thresholds. Separate knobs prevent tuning one feature from harming another.

### D8: Debug dump remains optional and bounded

When `CONFIG_MONITOR_DEBUG_DUMP` is enabled, extend dump content with collapsed extrema count and pair amplitudes/times. Do not dump full centerline arrays by default.

Rationale: pair diagnostics are enough to compare firmware with notebook while avoiding large SD writes during repeated events.

## Risks / Trade-offs

- Pair detection fails on low-SNR events -> return 0.0 modal metrics and keep sway metrics.
- Re-excitation inside DISTURBED can produce non-monotonic pair envelope -> stop damping envelope when amplitude increases, matching current conservative behavior.
- Piecewise-linear centerline can introduce sharp residual corners -> bounded FFT search and lobe collapse reduce impact, but notebook comparison remains required.
- Extra scratch buffer increases RAM -> use one reusable buffer and check map file after build.
- 12 Hz max exceeds Nyquist when ODR is below 24 Hz -> clamp max bin to available bins and document effective max frequency.
- Analysis on monitor task can lengthen DISTURBED->IDLE processing -> measure runtime with `esp_timer_get_time()` under debug and keep all passes O(N).

## Migration Plan

1. Add private modal-analysis structs and Kconfig parameters.
2. Implement and unit-test lobe collapse, centerline pair generation, residual generation, bounded FFT bin selection, and pair-envelope damping.
3. Wire DISTURBED->IDLE analysis to use residual FFT and pair-envelope damping.
4. Keep existing MonitorResult fields and payloads unchanged.
5. Validate against notebook output for `logs/raw_log_7.csv` and a synthetic drifting-baseline decay.
6. Build with ESP-IDF v5.5.4 for ESP32-S3 and inspect RAM/flash impact.

Rollback: set implementation behind a build-time config if needed, or restore raw-tilt FFT/damping path while keeping new helpers uncalled.
