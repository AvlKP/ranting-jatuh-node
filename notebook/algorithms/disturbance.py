import numpy as np
from typing import Tuple, List, Optional


def gyro_magnitude(gx: np.ndarray, gy: np.ndarray, gz: np.ndarray) -> np.ndarray:
    """Compute gyroscope magnitude from 3-axis data."""
    return np.sqrt(gx**2 + gy**2 + gz**2)


def detect_segments(
    gmag: np.ndarray,
    baseline: float,
    threshold: float = 0.35,
    min_samples: int = 5,
) -> List[Tuple[int, int]]:
    """
    Find segments where gyro magnitude exceeds baseline + threshold.

    Returns list of (start_idx, end_idx) for each active segment.
    Above-threshold samples within two indices of each other are merged.
    """
    above = gmag > baseline + threshold
    segments = []
    in_seg = False
    seg_start = 0

    for i, val in enumerate(above):
        if val and not in_seg:
            seg_start = i
            in_seg = True
        elif not val and in_seg:
            if i - seg_start >= min_samples:
                segments.append((seg_start, i - 1))
            in_seg = False
    if in_seg and len(above) - seg_start >= min_samples:
        segments.append((seg_start, len(above) - 1))

    # Merge segments within 2 samples of each other
    merged = []
    for s, e in segments:
        if merged and s - merged[-1][1] <= 2:
            merged[-1] = (merged[-1][0], e)
        else:
            merged.append((s, e))
    return merged


def find_boundaries(
    gmag: np.ndarray,
    t0_approx: int,
    t1_approx: int,
    baseline: float,
    threshold: float = 0.35,
    above: bool = True,
) -> Tuple[int, int]:
    """
    Refine segment boundaries by walking outward from approximate
    indices to find exact baseline-crossing points.

    Returns (start_idx, end_idx).
    """
    if above:
        # Walk backward from t0 to find first index above threshold
        start = t0_approx
        for i in range(t0_approx, max(0, t0_approx - 100), -1):
            if gmag[i] <= baseline + threshold:
                start = i + 1
                break
        # Walk forward from t1 to find first index below threshold
        end = t1_approx
        for i in range(t1_approx, min(len(gmag), t1_approx + 100)):
            if gmag[i] <= baseline + threshold:
                end = i
                break
    else:
        # Below-threshold detection (for events that reduce gyro activity)
        start = t0_approx
        for i in range(t0_approx, max(0, t0_approx - 100), -1):
            if gmag[i] >= baseline - threshold:
                start = i + 1
                break
        end = t1_approx
        for i in range(t1_approx, min(len(gmag), t1_approx + 100)):
            if gmag[i] >= baseline - threshold:
                end = i
                break

    return start, end


def classify_disturbance(
    gmag_segment: np.ndarray,
    gz_segment: np.ndarray,
    peak_gmag: float,
    baseline: float,
) -> str:
    """
    Classify a disturbance segment into one of:
      - 'pull_hold': sustained gyro offset, moderate peak
      - 'oscillation': rhythmic variation, moderate peak, long duration
      - 'pull_release': large impulse spike, subsequent decay
      - 'flick': brief, sharp impulse, smaller than pull_release
    """
    dur = len(gmag_segment)
    gmag_std = np.std(gmag_segment)
    gz_range = np.max(gz_segment) - np.min(gz_segment)

    if peak_gmag < 10.0 and dur > 100:
        return "oscillation"
    if peak_gmag > 40.0:
        return "pull_release"
    if peak_gmag > 15.0 and dur < 80:
        return "pull_release"
    if peak_gmag < 15.0 and dur < 80 and gz_range > 3.0:
        return "flick"
    if peak_gmag < 15.0 and dur > 50:
        return "pull_hold"

    return "unknown"


def detect_impulse(accel_mag: np.ndarray, threshold: float = 2.0) -> np.ndarray:
    """Legacy: detect sharp changes in acceleration magnitude."""
    diff = np.abs(np.diff(accel_mag))
    return np.where(diff > threshold)[0]
