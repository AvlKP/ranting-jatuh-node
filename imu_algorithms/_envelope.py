"""Unified envelope extraction and decay onset detection.

Provides:
- Peak-hold envelope (ESP32-feasible: O(N), single multiply-add per sample)
- TKEO energy-burst decay onset (type-agnostic)
- Envelope-based damping ratio via OLS linear regression on log-envelope

No SciPy dependency. No legacy I/Q demodulation, LPF envelope, or
backward running-minimum decay onset.
"""

import math
from enum import Enum
from typing import Any, Dict, Optional, Tuple

import numpy as np


class DecayQuality(Enum):
    """Decay region quality classification."""
    RELIABLE = "reliable"
    LOW = "low"
    NONE = "none"


def envelope_peak_hold(gmag: np.ndarray, dt: float, fc: float = 2.0) -> np.ndarray:
    """Extract amplitude envelope via asymmetric peak-hold detector.

    peak_hold[0] = gmag[0]
    peak_hold[n] = max(gmag[n], alpha * peak_hold[n-1])
    where alpha = exp(-2*pi * fc * dt)

    The peak-hold rises instantly with the signal (no ringing at PR spikes)
    and decays exponentially between peaks with time constant tau = 1/(2*pi*fc).

    ESP32-feasible: O(N) single-pass, one multiply-add per sample.
    """
    gmag = np.atleast_1d(np.asarray(gmag, dtype=np.float64))

    if dt <= 0:
        raise ValueError(f"dt must be positive, got {dt}")

    n = len(gmag)
    if n == 0:
        return np.array([], dtype=np.float32)

    alpha = np.exp(-2.0 * np.pi * fc * dt)
    envelope = np.empty(n, dtype=np.float32)

    envelope[0] = max(0.0, float(gmag[0]))
    for i in range(1, n):
        val = max(0.0, float(gmag[i]))
        envelope[i] = max(val, alpha * envelope[i - 1])

    return envelope


# -- TKEO energy-burst helpers --


def _tkeo_pos(gmag: np.ndarray) -> np.ndarray:
    """Compute non-negative Teager-Kaiser energy of gyro magnitude.

    psi[n] = gmag[n]^2 - gmag[n-1] * gmag[n+1]
    psi_pos[n] = max(psi[n], 0)

    Boundary: psi_pos[0] = psi_pos[-1] = 0.
    """
    n = len(gmag)
    psi_pos = np.zeros(n, dtype=np.float64)
    if n >= 3:
        psi = gmag[1:-1] ** 2 - gmag[0:-2] * gmag[2:]
        psi_pos[1:-1] = np.maximum(psi, 0.0)
    return psi_pos


def _local_maxima_indices(signal: np.ndarray) -> np.ndarray:
    """Return indices of local maxima in a 1-D signal."""
    n = len(signal)
    if n < 3:
        return np.array([], dtype=np.int64)
    peaks = []
    for i in range(1, n - 1):
        if signal[i] > signal[i - 1] and signal[i] > signal[i + 1]:
            peaks.append(i)
    return np.array(peaks, dtype=np.int64)


# -- Decay onset --


def find_decay_onset_tkeo(
    gmag: np.ndarray,
    dt: float,
    baseline_gmag: float = 0.35,
    q: float = 0.30,
    snap_window_s: float = 0.45,
    min_decay_samples: int = 20,
) -> Tuple[int, DecayQuality]:
    """Identify free-decay onset via TKEO energy-burst detection.

    Type-agnostic: works for PR, flick, oscillation without event_type.

    Algorithm:
    1. Compute non-negative TKEO energy (psi_pos) over gmag segment.
    2. energy_floor = (10 * baseline_gmag)^2 rejects pull-hold/static noise.
    3. threshold = max(energy_floor, q * max(psi_pos)) selects last burst.
    4. Find last index where psi_pos > threshold.
    5. Snap to nearest local gmag maximum within snap_window_s.
    6. Validate ringdown: min decay samples, 2x amplitude drop.

    Returns:
        (decay_onset, quality) where decay_onset is the sample index
        of free-decay start and quality indicates region reliability.
    """
    n = len(gmag)
    if n < 3:
        return 0, DecayQuality.NONE

    psi_pos = _tkeo_pos(gmag)

    energy_floor = (10.0 * baseline_gmag) ** 2
    psi_max = float(np.max(psi_pos))

    if psi_max < energy_floor:
        return 0, DecayQuality.NONE

    threshold = max(energy_floor, q * psi_max)

    last_burst = -1
    for i in range(n - 1, 1, -1):
        if psi_pos[i] > threshold:
            last_burst = i
            break

    if last_burst < 0:
        return 0, DecayQuality.NONE

    snap_samples = max(1, int(round(snap_window_s / dt)))
    snap_start = max(0, last_burst - snap_samples)
    snap_end = min(n - 1, last_burst + snap_samples)

    snap_window = gmag[snap_start:snap_end + 1]
    peak_indices_rel = _local_maxima_indices(snap_window)

    if len(peak_indices_rel) > 0:
        peak_indices_abs = peak_indices_rel + snap_start
        distances = np.abs(peak_indices_abs - last_burst)
        nearest_idx = int(np.argmin(distances))
        decay_onset = int(peak_indices_abs[nearest_idx])
    else:
        offset_in_window = int(np.argmax(snap_window))
        decay_onset = snap_start + offset_in_window

    decay_onset = max(0, min(decay_onset, n - 1))

    decay_len = n - decay_onset

    if decay_len < min_decay_samples:
        return decay_onset, DecayQuality.LOW

    onset_region_end = min(n - 1, decay_onset + min_decay_samples)
    onset_max = float(np.max(gmag[decay_onset:onset_region_end + 1]))

    tail_start = max(decay_onset, n - max(min_decay_samples, decay_len // 4))
    tail_min = float(np.min(gmag[tail_start:]))

    if tail_min < 1e-9 or onset_max / tail_min < 2.0:
        quality = DecayQuality.LOW
    else:
        quality = DecayQuality.RELIABLE

    return decay_onset, quality


# -- Damping --


def damping_from_envelope(
    envelope: np.ndarray,
    dt: float,
    fn: float,
    baseline_noise: float = 0.0,
    skip_transient_cycles: int = 0,
    lower_bound_override: Optional[float] = None,
    min_fit_samples: int = 10,
    min_fit_cycles: float = 2.0,
    min_amplitude_drop: float = 2.0,
    min_spc_for_high_conf: float = 4.0,
    return_diagnostics: bool = False,
) -> Tuple[float, str]:
    """Estimate damping ratio zeta via OLS linear regression on log-envelope.

    Fits a line to ln(envelope) in the decay region:
        ln(a(t)) = ln(A0) - zeta*omega_n*t
        slope = -zeta*omega_n
        zeta = -slope / omega_n

    Uses single-pass accumulator for OLS (no matrix allocation, ESP32-friendly).

    When baseline_noise > 0 (bounded fit mode):
        - Skips onset/filter transient samples.
        - Computes a lower fit bound from noise and peak envelope.
        - Fits only contiguous samples above the lower bound.
        - Applies sample-count, cycle-count, amplitude-drop gates.
        - Returns zeta=0.0 with "low" confidence if gates fail.
    """
    n = len(envelope)
    diag: Dict[str, Any] = {
        "fit_sample_count": 0,
        "fit_cycle_count": 0.0,
        "amplitude_drop": 1.0,
        "r_squared": 0.0,
        "lower_fit_bound": 0.0,
        "failure_gate": "",
        "valid_mask": np.zeros(n, dtype=bool),
    }

    def _fail(gate: str) -> Tuple:
        diag["failure_gate"] = gate
        if return_diagnostics:
            return 0.0, "low", diag
        return 0.0, "low"

    if fn <= 0:
        return _fail("fn_invalid")

    if n < min_fit_samples:
        return _fail("too_few_total_samples")

    period_samples = 1.0 / (fn * dt) if fn > 0 and dt > 0 else 0.0
    samples_per_cycle = period_samples

    skip_samples = 0
    use_bounded = baseline_noise > 0.0

    if use_bounded and skip_transient_cycles > 0 and period_samples > 0:
        skip_samples = min(
            int(round(skip_transient_cycles * period_samples)),
            n - min_fit_samples,
        )
        skip_samples = max(0, skip_samples)

    envelope_active = envelope[skip_samples:]
    n_active = len(envelope_active)

    if n_active < min_fit_samples:
        return _fail("too_few_samples_after_skip")

    if use_bounded:
        peak_env = float(np.max(envelope_active)) if n_active > 0 else 0.0

        if lower_bound_override is not None:
            lower_bound = max(lower_bound_override, 1e-6)
        else:
            lower_bound = max(
                4.0 * baseline_noise,
                0.03 * peak_env,
                1e-6,
            )

        diag["lower_fit_bound"] = float(lower_bound)

        valid_mask_active = envelope_active > lower_bound
    else:
        lower_bound = 1e-6
        diag["lower_fit_bound"] = lower_bound
        valid_mask_active = envelope_active > lower_bound

    env_valid = envelope_active[valid_mask_active]
    n_valid = len(env_valid)
    diag["fit_sample_count"] = n_valid

    if n_valid < min_fit_samples:
        return _fail("too_few_fit_samples")

    env_max = float(np.max(env_valid))
    env_min = float(np.min(env_valid))
    amp_drop = env_max / env_min if env_min > 1e-9 else float("inf")
    diag["amplitude_drop"] = float(round(amp_drop, 2))

    if env_min < 1e-9:
        return _fail("envelope_min_zero")
    if amp_drop < min_amplitude_drop:
        return _fail("insufficient_amplitude_drop")

    duration_s = n_valid * dt
    fit_cycles = duration_s * fn if fn > 0 else 0.0
    diag["fit_cycle_count"] = float(round(fit_cycles, 1))

    if fit_cycles < min_fit_cycles:
        return _fail("too_few_fit_cycles")

    valid_indices_relative = np.where(valid_mask_active)[0]
    t = (valid_indices_relative + skip_samples).astype(np.float64) * dt
    log_env = np.log(env_valid.astype(np.float64))

    n_v = float(len(t))
    sum_t = float(np.sum(t))
    sum_y = float(np.sum(log_env))
    sum_ty = float(np.sum(t * log_env))
    sum_t2 = float(np.sum(t * t))

    denominator = n_v * sum_t2 - sum_t * sum_t
    if abs(denominator) < 1e-15:
        return _fail("ols_denominator_zero")

    slope_numerator = n_v * sum_ty - sum_t * sum_y
    slope = slope_numerator / denominator

    if slope > 0:
        return _fail("positive_slope")

    mean_y = sum_y / n_v
    ss_res = 0.0
    ss_tot = 0.0
    intercept = (sum_y - slope * sum_t) / n_v
    for i in range(int(n_v)):
        predicted = intercept + slope * t[i]
        ss_res += (log_env[i] - predicted) ** 2
        ss_tot += (log_env[i] - mean_y) ** 2

    if ss_tot < 1e-15:
        return _fail("ss_tot_zero")

    r_squared = 1.0 - ss_res / ss_tot
    diag["r_squared"] = float(round(r_squared, 4))

    omega_n = 2.0 * np.pi * fn
    zeta = -slope / omega_n

    if not np.isfinite(zeta):
        return _fail("zeta_not_finite")

    zeta = float(np.clip(zeta, 0.0, 1.0))

    full_valid_mask = np.zeros(n, dtype=bool)
    full_valid_mask[skip_samples:][valid_mask_active] = True
    diag["valid_mask"] = full_valid_mask

    if r_squared > 0.90 and fit_cycles >= 3.0 and amp_drop >= min_amplitude_drop and samples_per_cycle >= min_spc_for_high_conf:
        confidence = "high"
    elif r_squared > 0.70 and amp_drop >= min_amplitude_drop:
        confidence = "medium"
    else:
        confidence = "low"

    if return_diagnostics:
        return zeta, confidence, diag
    return zeta, confidence
