"""DSP-based disturbance detection and biomechanical parameter extraction.

Two-plane architecture:
  Plane 1 (real-time): per-sample TKEO → Schmitt trigger state machine → ring buffer
  Plane 2 (offline):   FFT-based natural frequency, log-decrement damping,
                        RMS sway amplitude, tilt angles, event classification

All detection logic is self-contained: calibration, TKEO, state machine,
ring buffer, and extraction. The `disturbance.py` module is preserved as
ground-truth reference.
"""

from __future__ import annotations

import numpy as np
from dataclasses import dataclass, field
from enum import Enum, auto
from typing import List, Optional, Callable, Generator, Tuple

from algorithms.calibration import (
    calibrate, calibrated_gmag, BIAS_GX, BIAS_GY, BIAS_GZ,
)


# ═══════════════════════════════════════════════════════════════════
# 2. TKEO Filter
# ═══════════════════════════════════════════════════════════════════

def tkeo(signal: np.ndarray) -> np.ndarray:
    """Apply Teager-Kaiser Energy Operator to a 1-D signal.

    ψ[n] = x[n]² − x[n−1]·x[n+1]

    Boundary handling:
    - ψ[0] = x[0]  (copy first sample)
    - ψ[N−1] = x[N−1]  (copy last sample)

    Args:
        signal: 1-D array of calibrated gyro magnitude values.

    Returns:
        TKEO energy signal, same length as input.
    """
    n = len(signal)
    if n < 3:
        return np.copy(signal)

    result = np.empty(n, dtype=np.float64)
    # Copy boundaries
    result[0] = signal[0]
    result[-1] = signal[-1]
    # Core computation
    result[1:-1] = signal[1:-1]**2 - signal[0:-2] * signal[2:]
    return result


def tkeo_streaming(x_n: float, x_n1: float, x_n2: float) -> float:
    """Single-sample TKEO for real-time streaming path.

    Assumes a 3-sample sliding window where:
    - x_n   = x[n]   (current sample)
    - x_n1  = x[n−1] (previous sample)
    - x_n2  = x[n+1] (not yet available — caller passes x[n−2], x[n−1], x[n])

    In practice, the caller maintains a 3-sample history and evaluates
    TKEO on the middle sample once x[n+1] arrives:

        ψ[n] = x[n]² − x[n−1]·x[n+1]

    With 1-sample latency.

    Args:
        x_n:   x[n] — the middle sample being evaluated.
        x_n1:  x[n−1] — the sample before x_n.
        x_n2:  x[n+1] — the sample after x_n.

    Returns:
        TKEO energy value at sample n.
    """
    return x_n * x_n - x_n1 * x_n2


# ═══════════════════════════════════════════════════════════════════
# 3. Schmitt Trigger State Machine
# ═══════════════════════════════════════════════════════════════════

class State(Enum):
    """Disturbance detection state machine states."""
    IDLE = auto()
    ONSET = auto()
    ACTIVE = auto()
    QUIET = auto()
    OFFSET = auto()


@dataclass
class Event:
    """Detected disturbance event boundaries and running stats."""
    onset_idx: int
    offset_idx: int
    offset_write_ptr: int  # ring.write_ptr at offset time (for truncation check)
    peak_gmag: float
    dur_samples: int


class EventDetector:
    """Schmitt-trigger state machine for per-sample disturbance detection.

    Transitions:
        IDLE ──(TKEO > hi_thresh OR gmag > gmag_onset)──▶ ONSET ──▶ ACTIVE
        ACTIVE ──(TKEO < lo_thresh AND gmag < gmag_thresh)──▶ QUIET
        QUIET ──(TKEO > hi_thresh OR gmag >= gmag_thresh)──▶ ACTIVE
        QUIET ──(quiet >= min_quiet)──▶ OFFSET ──▶ IDLE

    Hysteresis (hi_thresh / lo_thresh) prevents chattering during
    low-amplitude oscillation phases. The minimum quiet period prevents
    false offsets during momentary amplitude dips.
    """

    def __init__(
        self,
        hi_thresh: float = 40.0,
        lo_thresh: float = 5.0,
        gmag_onset: float = 2.0,
        gmag_thresh: float = 1.5,
        min_quiet: int = 52,  # 1.0 s at 52 Hz — prevents event fragmentation
    ):
        self.hi_thresh = hi_thresh
        self.lo_thresh = lo_thresh
        self.gmag_onset = gmag_onset  # gmag above this triggers onset (fallback for pull-holds)
        self.gmag_thresh = gmag_thresh  # gmag must also drop below this for QUIET
        self.min_quiet = min_quiet
        self.reset()

    def reset(self) -> None:
        """Reset state machine to initial conditions."""
        self.state = State.IDLE
        self.onset_idx: int = 0
        self.offset_idx: int = 0
        self.peak_gmag: float = 0.0
        self.dur_samples: int = 0
        self.quiet_timer: int = 0
        self.write_ptr: int = 0  # sample counter (index into ring buffer)

    def process_sample(self, tkeo_val: float, gmag_val: float) -> Optional[Event]:
        """Process one sample through the state machine.

        Args:
            tkeo_val: TKEO energy at this sample.
            gmag_val: Calibrated gyro magnitude at this sample.

        Returns:
            Event if the state machine transitioned to OFFSET (disturbance
            ended), None otherwise.
        """
        if self.state == State.IDLE:
            # Onset: TKEO spike OR elevated gmag (catches pull-holds with minimal TKEO)
            if tkeo_val > self.hi_thresh or gmag_val > self.gmag_onset:
                self.onset_idx = self.write_ptr
                self.peak_gmag = gmag_val
                self.dur_samples = 1
                self.state = State.ONSET
            # else stay IDLE

        elif self.state == State.ONSET:
            # Transitional — one sample, immediately go ACTIVE
            self.peak_gmag = max(self.peak_gmag, gmag_val)
            self.dur_samples += 1
            self.state = State.ACTIVE

        elif self.state == State.ACTIVE:
            self.peak_gmag = max(self.peak_gmag, gmag_val)
            self.dur_samples += 1
            # Only transition to QUIET if BOTH TKEO and gmag are low
            # This prevents false QUIET during sustained oscillation with
            # low TKEO but high gmag
            if tkeo_val < self.lo_thresh and gmag_val < self.gmag_thresh:
                self.state = State.QUIET
                self.quiet_timer = 1

        elif self.state == State.QUIET:
            self.peak_gmag = max(self.peak_gmag, gmag_val)
            self.dur_samples += 1
            # Exit QUIET if either TKEO spikes OR gmag is still elevated
            if tkeo_val > self.hi_thresh or gmag_val >= self.gmag_thresh:
                # False quiet — disturbance resumed
                self.state = State.ACTIVE
                self.quiet_timer = 0
            elif tkeo_val < self.lo_thresh and gmag_val < self.gmag_thresh:
                self.quiet_timer += 1
                if self.quiet_timer >= self.min_quiet:
                    self.offset_idx = self.write_ptr
                    self.state = State.IDLE  # OFFSET → IDLE immediately
                    return Event(
                        onset_idx=self.onset_idx,
                        offset_idx=self.offset_idx,
                        offset_write_ptr=self.write_ptr,
                        peak_gmag=self.peak_gmag,
                        dur_samples=self.dur_samples,
                    )
            else:
                # Between lo and hi — stay in QUIET, don't reset timer
                pass

        elif self.state == State.OFFSET:
            # Should not reach here (OFFSET transitions to IDLE immediately)
            self.state = State.IDLE

        self.write_ptr += 1
        return None


# ═══════════════════════════════════════════════════════════════════
# 4. Ring Buffer
# ═══════════════════════════════════════════════════════════════════

class RingBuffer:
    """Power-of-2 circular buffer for storing calibrated gyro magnitude.

    Stores float32 values with cheap bitwise-modulo indexing.
    2048 samples covers 39.4 seconds at 52 Hz ODR — longer than
    any known event (max ~28.5s).
    """

    def __init__(self, size: int = 2048):
        # Ensure power of 2
        if size & (size - 1) != 0:
            raise ValueError(f"RingBuffer size must be power of 2, got {size}")
        self.SIZE = size
        self.MASK = size - 1
        self._buf = np.zeros(size, dtype=np.float32)
        self.write_ptr: int = 0  # absolute (unmasked) write index

    def write(self, value: float) -> None:
        """Write one sample and advance write pointer.

        Args:
            value: Calibrated gyro magnitude to store.
        """
        self._buf[self.write_ptr & self.MASK] = value
        self.write_ptr += 1

    def read_segment(self, start_idx: int, length: int) -> np.ndarray:
        """Copy a contiguous segment from the ring buffer.

        Handles wrap-around by copying in two parts if the segment
        crosses the buffer end.

        Args:
            start_idx: Absolute (unmasked) start index.
            length: Number of samples to copy.

        Returns:
            NumPy array of float32 values.
        """
        if length <= 0:
            return np.array([], dtype=np.float32)

        start_mod = start_idx & self.MASK
        end_mod = (start_idx + length - 1) & self.MASK

        if start_mod <= end_mod:
            # No wrap
            return self._buf[start_mod:end_mod + 1].copy()
        else:
            # Wrap around
            part1 = self._buf[start_mod:]
            part2 = self._buf[:end_mod + 1]
            return np.concatenate([part1, part2])

    def onset_age(self, onset_idx: int) -> int:
        """Samples between onset and current write pointer.

        Args:
            onset_idx: Absolute onset index (as stored by state machine).

        Returns:
            Number of samples since onset, modulo SIZE (for truncation check).
        """
        return self.write_ptr - onset_idx

    def is_truncated(self, onset_age: int) -> bool:
        """Check if onset predates the oldest buffered sample.

        Args:
            onset_age: Result from onset_age().

        Returns:
            True if the event onset is no longer in the buffer.
        """
        return onset_age > self.SIZE


# ═══════════════════════════════════════════════════════════════════
# 5. Parameter Extraction (Offline Plane)
# ═══════════════════════════════════════════════════════════════════

@dataclass
class EventResult:
    """All extracted parameters for a detected disturbance event."""
    event_type: str
    onset_idx: int
    offset_idx: int
    dur_samples: int
    peak_gmag: float
    natural_frequency_hz: float
    damping_ratio: float
    damping_confidence: str  # "high" for PR, "low" for oscillation
    sway_x_deg: float  # X-axis peak-to-peak angular displacement (degrees)
    sway_y_deg: float  # Y-axis peak-to-peak angular displacement (degrees)
    sway_z_deg: float  # Z-axis peak-to-peak angular displacement (degrees)
    tilt_x_deg: float
    tilt_y_deg: float
    is_truncated: bool
    # Raw segments for later inspection
    gmag_segment: np.ndarray = field(repr=False)
    ax_segment: np.ndarray = field(repr=False)
    ay_segment: np.ndarray = field(repr=False)
    az_segment: np.ndarray = field(repr=False)
    gx_segment: np.ndarray = field(repr=False, default_factory=lambda: np.array([], dtype=np.float32))
    gy_segment: np.ndarray = field(repr=False, default_factory=lambda: np.array([], dtype=np.float32))
    gz_segment: np.ndarray = field(repr=False, default_factory=lambda: np.array([], dtype=np.float32))


def extract_natural_frequency(segment: np.ndarray, dt: float) -> float:
    """Estimate dominant natural frequency via FFT.

    Uses the actual segment length (no forced padding to 256 — padding
    short segments creates strong DC artifacts). Applies a Hann window
    and scans bins corresponding to 0.5–25 Hz.

    Args:
        segment: Calibrated gyro magnitude segment (any length).
        dt: Sample interval in seconds.

    Returns:
        Dominant frequency in Hz, or 0.0 if insufficient data.
    """
    n = len(segment)
    if n < 4:
        return 0.0

    # Detrend and window (use actual length, no padding)
    detrended = segment - np.mean(segment)
    windowed = detrended * np.hanning(n)

    # Real FFT on actual segment
    fft = np.abs(np.fft.rfft(windowed))
    freqs = np.fft.rfftfreq(n, d=dt)

    if len(fft) <= 1:
        return 0.0

    # Scan bins: 0.5 Hz to 25 Hz
    lo_bin = max(1, int(0.5 * n * dt))
    hi_bin = min(len(fft) - 1, int(25.0 * n * dt) + 1)

    if lo_bin >= len(fft) or lo_bin >= hi_bin:
        return 0.0

    # Find peak in bandpass region (exclude DC bin 0)
    band = fft[lo_bin:hi_bin + 1]
    peak_rel = int(np.argmax(band))
    return float(freqs[lo_bin + peak_rel])


def extract_damping_gmag_legacy(
    segment: np.ndarray,
    dt: float,
    event_type: str = "unknown",
) -> Tuple[float, str]:
    """DEPRECATED: Estimate damping ratio via logarithmic decrement on gmag.

    This function is preserved for reference only. The new signed-axis
    implementation (`extract_damping`) replaces it.

    Finds local maxima in the segment, filters to post-global-max
    peaks (decay envelope), computes mean log decrement, and converts
    to damping ratio ζ.

    Confidence flag:
    - "high" for pull_release events (free vibration decay)
    - "low"  for oscillation events (driven response; envelope reflects
              driver energy input, not intrinsic damping)

    Args:
        segment: Calibrated gyro magnitude segment.
        dt: Sample interval (unused; kept for signature consistency).
        event_type: Event classification string.

    Returns:
        (zeta, confidence) where zeta is damping ratio (0–1)
        and confidence is "high" or "low".
    """
    # Find local maxima
    peaks = _find_local_maxima(segment)
    if len(peaks) < 3:
        return 0.0, "low"

    # Filter to post-global-max peaks (decay envelope)
    pk_max_idx = int(np.argmax(peaks))
    decay_peaks = peaks[pk_max_idx:]

    if len(decay_peaks) < 3:
        return 0.0, "low"

    # Compute log decrements
    deltas = []
    for i in range(len(decay_peaks) - 1):
        if decay_peaks[i] > 1e-6 and decay_peaks[i + 1] > 1e-6:
            deltas.append(np.log(decay_peaks[i] / decay_peaks[i + 1]))

    if not deltas:
        return 0.0, "low"

    delta = float(np.mean(deltas))
    zeta = delta / np.sqrt(4.0 * np.pi ** 2 + delta ** 2)

    confidence = "high" if event_type == "pull_release" else "low"
    return zeta, confidence


def extract_damping(
    signed_axis_seg: np.ndarray,
    dt: float,
    event_type: str = "unknown",
) -> Tuple[float, str]:
    """Estimate damping ratio via logarithmic decrement on signed axis.

    Uses zero-crossing detection to find half-cycle peaks, then computes
    log decrement from the decay tail. Only positive δ pairs (A_i > A_{i+1})
    contribute to the mean.

    Confidence rules:
    - PR events: "high" only if ≥3 strictly decreasing peak pairs exist.
    - Oscillation: "low" (driver-induced non-monotonicity).
    - Static: "low", ζ = 0.0.

    Args:
        signed_axis_seg: Signed dominant-axis gyro segment (already
            detrended by caller — subtract mean before passing).
        dt: Sample interval in seconds (unused; kept for signature
            consistency).
        event_type: Event classification string.

    Returns:
        (zeta, confidence) where zeta is damping ratio (0–1)
        and confidence is "high" or "low".
    """
    if event_type in ("pull_hold", "flick", "unknown"):
        return 0.0, "low"

    # Detrend to remove residual DC bias
    detrended = signed_axis_seg - np.mean(signed_axis_seg)

    # Zero-crossing detection
    zero_crossings = _find_zero_crossings(detrended)
    if len(zero_crossings) < 2:
        return 0.0, "low"

    # Half-cycle peaks
    peaks = _half_cycle_peaks(detrended, zero_crossings)
    if len(peaks) < 3:
        return 0.0, "low"

    # Filter to post-global-max peaks (decay tail, using absolute values)
    abs_peaks = np.abs(peaks)
    pk_max_idx = int(np.argmax(abs_peaks))
    decay_peaks = abs_peaks[pk_max_idx:]

    if len(decay_peaks) < 3:
        return 0.0, "low"

    # Compute log decrements — exclude negative δ pairs
    deltas = []
    decreasing_count = 0
    for i in range(len(decay_peaks) - 1):
        a_i = decay_peaks[i]
        a_j = decay_peaks[i + 1]
        if a_i > 1e-6 and a_j > 1e-6:
            delta_ij = np.log(a_i / a_j)
            if delta_ij > 0:
                deltas.append(delta_ij)
                decreasing_count += 1

    if not deltas:
        return 0.0, "low"

    delta = float(np.mean(deltas))
    zeta = delta / np.sqrt(4.0 * np.pi ** 2 + delta ** 2)

    # Confidence: PR events need ≥3 decreasing pairs for "high"
    if event_type == "pull_release":
        confidence = "high" if decreasing_count >= 3 else "low"
    else:
        confidence = "low"

    return zeta, confidence


def _find_local_maxima(signal: np.ndarray) -> np.ndarray:
    """Extract local maxima from 1-D signal."""
    peaks = []
    for i in range(1, len(signal) - 1):
        if signal[i] > signal[i - 1] and signal[i] > signal[i + 1]:
            peaks.append(signal[i])
    return np.array(peaks, dtype=np.float64)


def _find_zero_crossings(signal: np.ndarray) -> np.ndarray:
    """Find indices where the signed signal crosses zero.

    A zero-crossing is identified whenever the sign changes between
    consecutive samples. The returned index is i+1 (the first sample
    on the new side of the axis). Zeros are treated as their own sign
    (0); a transition from negative to zero also counts.

    Args:
        signal: 1-D array of signed values (e.g., detrended gyro axis).

    Returns:
        Integer array of zero-crossing indices (may be empty).
    """
    n = len(signal)
    if n < 2:
        return np.array([], dtype=np.int64)

    crossings = []
    # Use sign: negative → -1, zero → 0, positive → +1
    signs = np.sign(signal)
    for i in range(n - 1):
        if signs[i] != signs[i + 1]:
            crossings.append(i + 1)
    return np.array(crossings, dtype=np.int64)


def _half_cycle_peaks(
    signal: np.ndarray,
    zero_crossings: np.ndarray,
) -> np.ndarray:
    """Extract half-cycle peaks using zero-crossing intervals.

    For each interval between consecutive zero-crossings, finds the
    sample with maximum absolute value and records its signed value.
    This yields exactly one peak per half-cycle.

    Args:
        signal: 1-D signed array (same as passed to _find_zero_crossings).
        zero_crossings: Output from _find_zero_crossings.

    Returns:
        1-D array of signed half-cycle peak values (may be empty if
        fewer than 2 crossings exist).
    """
    n_cross = len(zero_crossings)
    if n_cross < 2:
        return np.array([], dtype=np.float64)

    peaks = []
    for k in range(n_cross - 1):
        a = zero_crossings[k]
        b = zero_crossings[k + 1]
        seg = signal[a:b]
        if len(seg) == 0:
            continue
        # Find sample with maximum absolute value
        idx_peak = int(np.argmax(np.abs(seg)))
        peaks.append(float(seg[idx_peak]))

    return np.array(peaks, dtype=np.float64)


def extract_sway_amplitude(segment: np.ndarray) -> float:
    """RMS sway amplitude (standard deviation of gyro magnitude).

    **DEPRECATED** — replaced by per-axis peak-to-peak angular displacement.
    Preserved as legacy reference; no longer called by the pipeline.

    Args:
        segment: Calibrated gyro magnitude segment.

    Returns:
        RMS amplitude in dps.
    """
    return float(np.std(segment))


def extract_active_region(
    segment: np.ndarray,
    peak_gmag: float,
    ratio: float = 0.1,
) -> Tuple[int, int]:
    """Identify the active sub-segment where branch motion exceeds threshold.

    Finds the contiguous block of samples where gmag > ratio * peak_gmag,
    from first above-threshold sample to last.

    Args:
        segment: Calibrated gyro magnitude segment (1-D array).
        peak_gmag: Peak gmag value within this segment.
        ratio: Fraction of peak_gmag to use as threshold (default 0.1).

    Returns:
        (active_start, active_end) indices relative to the segment.
        Falls back to (0, len(segment)-1) if fewer than 3 samples qualify.
    """
    threshold = ratio * peak_gmag
    above = np.where(segment > threshold)[0]

    if len(above) < 3:
        return 0, len(segment) - 1

    return int(above[0]), int(above[-1])


def extract_active_sway(
    gyro_x_seg: np.ndarray,
    gyro_y_seg: np.ndarray,
    gyro_z_seg: np.ndarray,
    active_start: int,
    active_end: int,
    dt: float,
) -> Tuple[float, float, float]:
    """Compute per-axis peak-to-peak angular displacement within active region.

    Integrates each signed gyro axis independently over the active window
    to obtain angular displacement in degrees, then returns peak-to-peak.

    Args:
        gyro_x_seg, gyro_y_seg, gyro_z_seg: Per-axis gyro segments (dps).
        active_start, active_end: Active region bounds (inclusive).
        dt: Sample interval in seconds.

    Returns:
        (sway_x_deg, sway_y_deg, sway_z_deg) — peak-to-peak angular
        displacement in degrees.
    """
    n_active = active_end - active_start + 1
    if n_active < 3:
        return 0.0, 0.0, 0.0

    def _axis_sway(axis_seg: np.ndarray) -> float:
        active = axis_seg[active_start:active_end + 1]
        # Only integrate if there's meaningful variance
        if active.max() - active.min() < 1e-6:
            return 0.0
        # Integrate: cumsum * dt gives angle in degrees
        angle = np.cumsum(active) * dt
        return float(angle.max() - angle.min())

    return (
        _axis_sway(gyro_x_seg),
        _axis_sway(gyro_y_seg),
        _axis_sway(gyro_z_seg),
    )


def extract_tilt(
    ax_segment: np.ndarray,
    ay_segment: np.ndarray,
    az_segment: np.ndarray,
) -> Tuple[float, float]:
    """Compute static tilt angles from accelerometer readings.

    Uses mean acceleration over the event segment and arcsin
    to compute tilt in degrees.

    Args:
        ax_segment, ay_segment, az_segment: Calibrated accel segments.

    Returns:
        (tilt_x_deg, tilt_y_deg).
    """
    ax_mean = float(np.mean(ax_segment))
    ay_mean = float(np.mean(ay_segment))
    tx = float(np.degrees(np.arcsin(np.clip(ax_mean, -1.0, 1.0))))
    ty = float(np.degrees(np.arcsin(np.clip(ay_mean, -1.0, 1.0))))
    return tx, ty


def classify_event(peak_gmag: float, dur_samples: int) -> str:
    """Classify disturbance type using corrected thresholds.

    Decision tree (validated against 10 manual labels):
        peak_gmag >= 20.0                    → pull_release
        peak_gmag >= 8.0  and dur > 80       → oscillation
        peak_gmag < 8.0   and dur < 80       → flick
        peak_gmag < 8.0   and dur >= 80      → pull_hold

    Args:
        peak_gmag: Peak calibrated gyro magnitude (dps).
        dur_samples: Event duration in samples.

    Returns:
        Classification string: "pull_release", "oscillation", "flick",
        "pull_hold", or "unknown".
    """
    if peak_gmag >= 20.0:
        return "pull_release"
    elif peak_gmag >= 8.0 and dur_samples > 80:
        return "oscillation"
    elif peak_gmag < 8.0 and dur_samples < 80:
        return "flick"
    elif peak_gmag < 8.0 and dur_samples >= 80:
        return "pull_hold"
    else:
        return "unknown"


def is_dynamic(event_type: str) -> bool:
    """Two-class grouping: dynamic events yield biomechanical parameters.

    Args:
        event_type: Classification string from classify_event().

    Returns:
        True for "pull_release" and "oscillation" (dynamic),
        False for "pull_hold", "flick", and "unknown" (static).
    """
    return event_type in ("pull_release", "oscillation")


# ═══════════════════════════════════════════════════════════════════
# 6. End-to-End Pipeline
# ═══════════════════════════════════════════════════════════════════

class Pipeline:
    """Composes detection + ring buffer + extraction into one pipeline.

    Usage (batch):
        pipe = Pipeline()
        results = pipe.process_csv("raw_log_7.csv")

    Usage (streaming preview):
        pipe = Pipeline()
        for event in pipe.process_streaming(callback):
            print(event)
    """

    def __init__(
        self,
        hi_thresh: float = 40.0,
        lo_thresh: float = 5.0,
        gmag_onset: float = 2.0,
        gmag_thresh: float = 1.5,
        min_quiet: int = 52,
        ring_size: int = 2048,
        dt: float = None,  # Auto-detected from data if None
    ):
        self.detector = EventDetector(
            hi_thresh=hi_thresh,
            lo_thresh=lo_thresh,
            gmag_onset=gmag_onset,
            gmag_thresh=gmag_thresh,
            min_quiet=min_quiet,
        )
        self.ring = RingBuffer(size=ring_size)
        self.dt = dt  # set in process_csv if None

    def reset(self) -> None:
        """Reset pipeline state for a new run."""
        self.detector.reset()
        self.ring = RingBuffer(size=self.ring.SIZE)

    def _process_row(
        self,
        gx: float, gy: float, gz: float,
        ax: float, ay: float, az: float,
    ) -> Optional[Event]:
        """Process one IMU sample row through plane 1.

        Calibrate → gmag → write ring → compute TKEO → state machine.
        """
        # Calibrate
        gx_c, gy_c, gz_c, _, _, _ = calibrate(gx, gy, gz, ax, ay, az)
        gmag_val = float(np.sqrt(gx_c ** 2 + gy_c ** 2 + gz_c ** 2))

        # Ring buffer write (stores gmag before TKEO for full history)
        self.ring.write(gmag_val)

        # TKEO requires 3-sample history — use ring buffer for boundary cases
        tkeo_val = self._compute_tkeo(gmag_val)

        # State machine
        return self.detector.process_sample(tkeo_val, gmag_val)

    def _compute_tkeo(self, gmag_val: float) -> float:
        """Compute TKEO from ring buffer history.

        ψ[n−1] = x[n−1]² − x[n−2]·x[n]

        After ring.write(gmag_val), write_ptr was advanced by 1.
        The ring buffer holds gmag history. To compute TKEO on the
        one-sample-behind (n−1), we need:
          x[n−1] = buf[(wp−2) & MASK]  (2 slots before next write)
          x[n−2] = buf[(wp−3) & MASK]  (3 slots before next write)
          x[n]   = gmag_val (the just-processed sample)

        First 2 samples return gmag_val (boundary handling).
        """
        wp = self.ring.write_ptr  # already advanced by _process_row
        if wp < 3:
            # Need at least 3 samples for a valid TKEO
            return gmag_val

        # Read x[n−1] and x[n−2] from 2 and 3 positions before next write
        idx_n1 = (wp - 2) & self.ring.MASK
        idx_n2 = (wp - 3) & self.ring.MASK
        x_n1 = float(self.ring._buf[idx_n1])  # gmag[n-1]
        x_n2 = float(self.ring._buf[idx_n2])  # gmag[n-2]

        # ψ[n−1] = x[n−1]² − x[n−2]·x[n]
        # tkeo_streaming(middle, left, right) = middle² − left·right
        return tkeo_streaming(x_n1, x_n2, gmag_val)

    def _extract_params(self, event: Event) -> EventResult:
        """Run offline plane: extract all parameters for one event.

        Copies segment from ring buffer, classifies, and computes
        biomechanical parameters.

        Per-axis sway is initialized to 0.0 here; the caller
        (process_csv) fills it from DataFrame gyro columns.
        """
        # Use offset_write_ptr for truncation check (not current write_ptr)
        onset_age = event.offset_write_ptr - event.onset_idx
        truncated = onset_age > self.ring.SIZE
        seg_len = min(onset_age, self.ring.SIZE)

        gmag_seg = self.ring.read_segment(event.onset_idx, seg_len)

        event_type = classify_event(event.peak_gmag, event.dur_samples)

        # Parameter extraction (some may fail gracefully)
        fn = extract_natural_frequency(gmag_seg, self.dt)
        zeta, conf = extract_damping_gmag_legacy(gmag_seg, self.dt, event_type)

        return EventResult(
            event_type=event_type,
            onset_idx=event.onset_idx,
            offset_idx=event.offset_idx,
            dur_samples=event.dur_samples,
            peak_gmag=event.peak_gmag,
            natural_frequency_hz=fn,
            damping_ratio=zeta,
            damping_confidence=conf,
            sway_x_deg=0.0,  # filled by caller from per-axis gyro data
            sway_y_deg=0.0,
            sway_z_deg=0.0,
            tilt_x_deg=0.0,  # filled by caller from accel data
            tilt_y_deg=0.0,
            is_truncated=truncated,
            gmag_segment=gmag_seg,
            ax_segment=np.array([], dtype=np.float32),
            ay_segment=np.array([], dtype=np.float32),
            az_segment=np.array([], dtype=np.float32),
        )

    def process_csv(self, filepath: str) -> List[EventResult]:
        """Full batch processing of a raw IMU CSV file.

        Args:
            filepath: Path to raw_log CSV.

        Returns:
            List of EventResult for each detected disturbance.
        """
        import pandas as pd

        cols = [
            "timestamp_us", "accel_x", "accel_y", "accel_z",
            "gyro_x", "gyro_y", "gyro_z", "temp_c",
        ]
        df = pd.read_csv(filepath, header=None, names=cols)

        # Auto-detect dt from timestamps
        if self.dt is None:
            ts = (df["timestamp_us"] - df["timestamp_us"].iloc[0]) / 1e6
            self.dt = float(np.median(np.diff(ts)))

        self.reset()
        events: List[Event] = []

        # Plane 1: real-time detection pass
        for _, row in df.iterrows():
            event = self._process_row(
                row["gyro_x"], row["gyro_y"], row["gyro_z"],
                row["accel_x"], row["accel_y"], row["accel_z"],
            )
            if event is not None:
                events.append(event)

        # Edge case: event still active at end of data
        if self.detector.state in (State.ACTIVE, State.QUIET, State.ONSET):
            wp = self.ring.write_ptr
            event = Event(
                onset_idx=self.detector.onset_idx,
                offset_idx=wp - 1,  # use last sample as offset
                offset_write_ptr=wp,
                peak_gmag=self.detector.peak_gmag,
                dur_samples=self.detector.dur_samples,
            )
            events.append(event)

        # Plane 2: offline parameter extraction
        results = []
        for event in events:
            result = self._extract_params(event)

            # Extract accel and gyro segments from DataFrame
            onset = event.onset_idx
            offset = event.offset_idx
            if onset < len(df) and offset < len(df):
                seg = df.iloc[onset:offset + 1]
                result.ax_segment = seg["accel_x"].values.astype(np.float32)
                result.ay_segment = seg["accel_y"].values.astype(np.float32)
                result.az_segment = seg["accel_z"].values.astype(np.float32)
                result.gx_segment = (seg["gyro_x"].values - BIAS_GX).astype(np.float32)
                result.gy_segment = (seg["gyro_y"].values - BIAS_GY).astype(np.float32)
                result.gz_segment = (seg["gyro_z"].values - BIAS_GZ).astype(np.float32)

                # Tilt from accel (unchanged)
                tx, ty = extract_tilt(
                    result.ax_segment, result.ay_segment, result.az_segment,
                )
                result.tilt_x_deg = tx
                result.tilt_y_deg = ty

                # Per-axis sway for dynamic events (active region from gmag)
                if is_dynamic(result.event_type):
                    active_start, active_end = extract_active_region(
                        result.gmag_segment, event.peak_gmag,
                    )
                    sx, sy, sz = extract_active_sway(
                        result.gx_segment, result.gy_segment, result.gz_segment,
                        active_start, active_end, self.dt,
                    )
                    result.sway_x_deg = sx
                    result.sway_y_deg = sy
                    result.sway_z_deg = sz

                    # Signed-axis damping: override gmag legacy with
                    # dominant-axis zero-crossing estimate
                    sway_values = [sx, sy, sz]
                    max_sway = max(sway_values)
                    if max_sway > 0.0:
                        dominant = int(np.argmax(sway_values))
                        axis_map = {
                            0: result.gx_segment,
                            1: result.gy_segment,
                            2: result.gz_segment,
                        }
                        signed_full_seg = axis_map[dominant]
                        # Extract active region from signed dominant axis
                        active_signed = signed_full_seg[active_start:active_end + 1]
                        zeta_signed, conf_signed = extract_damping(
                            active_signed, self.dt, result.event_type,
                        )
                        result.damping_ratio = zeta_signed
                        result.damping_confidence = conf_signed
                    # else: all sway 0.0 → keep gmag legacy fallback

            results.append(result)

        return results

    def process_streaming(
        self,
        sample_callback: Callable[[], Optional[Tuple[float, ...]]],
    ) -> Generator[EventResult, None, None]:
        """Streaming interface: yields EventResult as events complete.

        The caller provides a callback that returns (gx, gy, gz, ax, ay, az)
        tuples, or None when the stream ends.

        Args:
            sample_callback: Function returning next IMU sample tuple or None.

        Yields:
            EventResult for each completed disturbance.
        """
        self.reset()

        while True:
            sample = sample_callback()
            if sample is None:
                break
            gx, gy, gz, ax, ay, az = sample
            event = self._process_row(gx, gy, gz, ax, ay, az)
            if event is not None:
                yield self._extract_params(event)


# ═══════════════════════════════════════════════════════════════════
# 7. Ground Truth Validation
# ═══════════════════════════════════════════════════════════════════

# Manual labels from exploration notebook (seconds)
MANUAL_LABELS = [
    (1,  "pull_hold",     8.5,  13.0),
    (2,  "pull_hold",     17.5,  21.0),
    (3,  "oscillation",   35.5,  64.0),
    (4,  "oscillation",   68.9,  87.0),
    (5,  "pull_release",  95.0,  98.5),
    (6,  "pull_release",  102.5, 106.5),
    (7,  "pull_release",  110.5, 115.0),
    (8,  "flick",         127.7, 129.5),
    (9,  "pull_release",  130.5, 132.5),
    (10, "pull_release",  139.0, 145.0),
]


def validate_against_ground_truth(
    detected: List[EventResult],
    manual_labels: List[tuple] = None,
    timestamps_s: np.ndarray = None,
) -> dict:
    """Compare detected events against manually labeled ground truth.

    Computes:
    - Time-overlap IoU between detected boundaries and manual labels
    - Classification confusion matrix
    - Per-event parameter comparison table

    Args:
        detected: List of EventResult from Pipeline.process_csv().
        manual_labels: List of (num, label, t0_s, t1_s) tuples.
                       Defaults to MANUAL_LABELS.
        timestamps_s: Array of timestamps in seconds (same length as data).
                      Used for time-to-index mapping. If None, uses dt=0.03
                      (approximate for this dataset).

    Returns:
        Dict with keys: iou_scores, confusion, per_event_comparison,
        summary.
    """
    if manual_labels is None:
        manual_labels = MANUAL_LABELS

    n_gt = len(manual_labels)
    n_det = len(detected)

    # Convert ground truth times to indices
    gt_ranges = []
    for num, label, t0, t1 in manual_labels:
        if timestamps_s is not None:
            i0 = int(np.searchsorted(timestamps_s, t0))
            i1 = int(np.searchsorted(timestamps_s, t1))
        else:
            # Fallback: approximate with dt=0.03
            i0 = int(t0 / 0.03)
            i1 = int(t1 / 0.03)
        gt_ranges.append((num, label, i0, i1, t0, t1))

    # Convert detected to index ranges
    det_ranges = []
    for i, r in enumerate(detected):
        det_ranges.append((i + 1, r.event_type, r.onset_idx, r.offset_idx))

    # IoU matching: for each ground truth, find best-matching detection
    iou_scores = []
    matched_det = set()
    for gt_num, gt_label, gt_i0, gt_i1, gt_t0, gt_t1 in gt_ranges:
        best_iou = 0.0
        best_det_idx = None
        for det_idx, det_label, det_i0, det_i1 in det_ranges:
            # Intersection over union
            inter_start = max(gt_i0, det_i0)
            inter_end = min(gt_i1, det_i1)
            if inter_start < inter_end:
                inter = inter_end - inter_start
                union = max(gt_i1, det_i1) - min(gt_i0, det_i0)
                iou = inter / union if union > 0 else 0.0
                if iou > best_iou:
                    best_iou = iou
                    best_det_idx = det_idx
        iou_scores.append({
            "gt_num": gt_num,
            "gt_label": gt_label,
            "gt_time": (gt_t0, gt_t1),
            "iou": best_iou,
            "matched_det": best_det_idx,
        })
        if best_det_idx is not None:
            matched_det.add(best_det_idx)

    # Classification confusion: compare matched GT→det pairs
    confusion = {"correct": 0, "mismatch": [], "unmatched_gt": [], "unmatched_det": []}
    for score in iou_scores:
        if score["iou"] > 0.5 and score["matched_det"] is not None:
            det_type = det_ranges[score["matched_det"] - 1][1]
            if det_type == score["gt_label"]:
                confusion["correct"] += 1
            else:
                confusion["mismatch"].append({
                    "gt_num": score["gt_num"],
                    "gt_label": score["gt_label"],
                    "det_label": det_type,
                    "iou": score["iou"],
                })
        else:
            confusion["unmatched_gt"].append(score["gt_num"])

    # Unmatched detections
    for det_idx in range(1, n_det + 1):
        if det_idx not in matched_det:
            confusion["unmatched_det"].append(det_idx)

    # Per-event parameter comparison
    per_event = []
    for score in iou_scores:
        entry = {
            "gt_num": score["gt_num"],
            "gt_label": score["gt_label"],
            "iou": score["iou"],
        }
        if score["matched_det"] is not None:
            det = detected[score["matched_det"] - 1]
            entry.update({
                "det_label": det.event_type,
                "fn_hz": det.natural_frequency_hz,
                "zeta": det.damping_ratio,
                "zeta_conf": det.damping_confidence,
                "sway_x_deg": det.sway_x_deg,
                "sway_y_deg": det.sway_y_deg,
                "sway_z_deg": det.sway_z_deg,
                "tilt_x": det.tilt_x_deg,
                "tilt_y": det.tilt_y_deg,
                "truncated": det.is_truncated,
            })
        per_event.append(entry)

    # Summary
    mean_iou = np.mean([s["iou"] for s in iou_scores]) if iou_scores else 0.0
    summary = {
        "n_ground_truth": n_gt,
        "n_detected": n_det,
        "mean_iou": mean_iou,
        "classification_accuracy": (
            confusion["correct"] / n_gt if n_gt > 0 else 0.0
        ),
        "unmatched_gt": confusion["unmatched_gt"],
        "unmatched_det": confusion["unmatched_det"],
    }

    return {
        "iou_scores": iou_scores,
        "confusion": confusion,
        "per_event": per_event,
        "summary": summary,
    }
