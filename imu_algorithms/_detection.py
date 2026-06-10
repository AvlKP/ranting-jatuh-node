"""Disturbance detection: TKEO filter + Schmitt trigger state machine.

Plane 1 (real-time): per-sample TKEO -> Schmitt trigger -> ring buffer.
ESP32-feasible: O(1) per sample, fixed state, no allocation.
"""

from dataclasses import dataclass
from enum import Enum, auto
from typing import Optional

import numpy as np


def tkeo(signal: np.ndarray) -> np.ndarray:
    """Apply Teager-Kaiser Energy Operator to a 1-D signal.

    psi[n] = x[n]^2 - x[n-1] * x[n+1]

    Boundary handling:
      - psi[0]    = x[0]   (copy first sample)
      - psi[N-1]  = x[N-1] (copy last sample)

    Args:
        signal: 1-D numpy array of signal values.

    Returns:
        TKEO energy array of same length as input.
    """
    n = len(signal)
    if n < 3:
        return np.copy(signal)

    result = np.empty(n, dtype=np.float64)
    result[0] = signal[0]
    result[-1] = signal[-1]
    result[1:-1] = signal[1:-1]**2 - signal[0:-2] * signal[2:]
    return result


def tkeo_streaming(x_n: float, x_n1: float, x_n2: float) -> float:
    """Single-sample TKEO for real-time streaming path.

    psi[n] = x[n]^2 - x[n-1] * x[n+1]

    Computes energy at the middle sample with 1-sample latency.
    Designed for embedded deployment: one multiply-add per sample.

    Args:
        x_n:   x[n]      -- the middle sample being evaluated.
        x_n1:  x[n-1]    -- the sample before x_n.
        x_n2:  x[n+1]    -- the sample after x_n.

    Returns:
        TKEO energy value at sample n.
    """
    return x_n * x_n - x_n1 * x_n2


class State(Enum):
    """Disturbance detection state machine states.

    Two-state design with a quiet_timer counter inside ACTIVE:
      IDLE  -- waiting for onset
      ACTIVE -- disturbance in progress (quiet_timer > 0 = quiet-counting mode)
    """
    IDLE = auto()
    ACTIVE = auto()


@dataclass
class Event:
    """Detected disturbance event boundaries and running stats.

    Attributes:
        onset_idx: Sample index where disturbance began.
        offset_idx: Sample index where disturbance ended.
        offset_write_ptr: Current write pointer at offset time.
        peak_gmag: Maximum gyro magnitude during event [dps].
        dur_samples: Total duration in samples.
    """
    onset_idx: int
    offset_idx: int
    offset_write_ptr: int
    peak_gmag: float
    dur_samples: int


class EventDetector:
    """Schmitt-trigger state machine for per-sample disturbance detection.

    Two-state design with a quiet_timer counter in ACTIVE:

      IDLE --(TKEO > hi_thresh OR gmag > gmag_onset)--> ACTIVE
      ACTIVE:
        quiet_timer == 0:  normal-active mode
          --(TKEO < lo_thresh AND gmag < gmag_thresh)--> quiet_timer = 1
        quiet_timer > 0:   quiet-counting mode
          --(TKEO > hi_thresh OR gmag >= gmag_thresh)--> quiet_timer = 0  (re-entry)
          --(quiet_timer >= min_quiet)--> emit Event -> IDLE

    Hysteresis (hi_thresh / lo_thresh) prevents chattering during
    low-amplitude oscillation phases. The minimum quiet period prevents
    false offsets during momentary amplitude dips.

    Attributes:
        hi_thresh: TKEO high threshold for disturbance entry (default 40.0).
        lo_thresh: TKEO low threshold for quiet counting (default 5.0).
        gmag_onset: Gyro magnitude onset threshold for immediate entry [dps].
        gmag_thresh: Gyro magnitude threshold for quiet counting [dps].
        min_quiet: Minimum consecutive quiet samples for IDLE transition.
    """

    def __init__(
        self,
        hi_thresh: float = 40.0,
        lo_thresh: float = 5.0,
        gmag_onset: float = 2.0,
        gmag_thresh: float = 1.5,
        min_quiet: int = 52,
    ):
        self.hi_thresh = hi_thresh
        self.lo_thresh = lo_thresh
        self.gmag_onset = gmag_onset
        self.gmag_thresh = gmag_thresh
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
        self.write_ptr: int = 0

    def process_sample(self, tkeo_val: float, gmag_val: float) -> Optional[Event]:
        """Process one sample through the state machine.

        Args:
            tkeo_val: TKEO energy value from tkeo_streaming().
            gmag_val: Calibrated gyro magnitude [dps].

        Returns:
            Event when quiet_timer reaches min_quiet (disturbance ended),
            None otherwise.
        """
        if self.state == State.IDLE:
            if tkeo_val > self.hi_thresh or gmag_val > self.gmag_onset:
                self.onset_idx = self.write_ptr
                self.peak_gmag = gmag_val
                self.dur_samples = 1
                self.quiet_timer = 0
                self.state = State.ACTIVE

        elif self.state == State.ACTIVE:
            self.peak_gmag = max(self.peak_gmag, gmag_val)
            self.dur_samples += 1

            if self.quiet_timer > 0:
                if tkeo_val > self.hi_thresh or gmag_val >= self.gmag_thresh:
                    self.quiet_timer = 0
                elif tkeo_val < self.lo_thresh and gmag_val < self.gmag_thresh:
                    self.quiet_timer += 1
                    if self.quiet_timer >= self.min_quiet:
                        self.offset_idx = self.write_ptr
                        self.state = State.IDLE
                        return Event(
                            onset_idx=self.onset_idx,
                            offset_idx=self.offset_idx,
                            offset_write_ptr=self.write_ptr,
                            peak_gmag=self.peak_gmag,
                            dur_samples=self.dur_samples,
                        )
            else:
                if tkeo_val < self.lo_thresh and gmag_val < self.gmag_thresh:
                    self.quiet_timer = 1

        self.write_ptr += 1
        return None


def classify_event(peak_gmag: float, dur_samples: int) -> str:
    """Classify disturbance type from peak magnitude and duration.

    Decision tree:
        peak_gmag >= 20.0                    -> pull_release
        peak_gmag >= 8.0  and dur > 80       -> oscillation
        peak_gmag < 8.0   and dur < 80       -> flick
        peak_gmag < 8.0   and dur >= 80      -> pull_hold

    This function exists only in the Python reference. The C++ firmware
    treats all disturbances uniformly (no event type classification).
    Kept as algorithmic reference for future C++ porting.

    Args:
        peak_gmag: Maximum gyro magnitude during event [dps].
        dur_samples: Event duration in samples.

    Returns:
        Event type string: "pull_release", "oscillation", "flick",
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
    """Check if event type is dynamic (yields biomechanical parameters).

    Dynamic events: pull_release, oscillation.
    Non-dynamic: flick, pull_hold, unknown.

    This function exists only in the Python reference. The C++ firmware
    computes damping on all disturbances without event type gating.

    Args:
        event_type: Event type string from classify_event().

    Returns:
        True if the event is a dynamic type suitable for damping analysis.
    """
    return event_type in ("pull_release", "oscillation")
