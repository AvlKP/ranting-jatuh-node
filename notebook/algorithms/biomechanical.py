import numpy as np
from typing import Tuple, List


def natural_frequency(signal: np.ndarray, dt: float) -> float:
    """
    Estimate dominant natural frequency via FFT.

    Strips DC, applies a Hann window, and returns the frequency
    of the largest spectral peak (excluding DC).
    """
    n = len(signal)
    if n < 4:
        return 0.0
    detrended = signal - np.mean(signal)
    windowed = detrended * np.hanning(n)
    fft = np.abs(np.fft.rfft(windowed))
    freqs = np.fft.rfftfreq(n, d=dt)
    if len(fft) <= 1:
        return 0.0
    peak = np.argmax(fft[1:]) + 1
    return float(freqs[peak])


def _find_peaks(signal: np.ndarray) -> List[float]:
    """Extract local maxima from 1-D signal."""
    peaks = []
    for i in range(1, len(signal) - 1):
        if signal[i] > signal[i - 1] and signal[i] > signal[i + 1]:
            peaks.append(float(signal[i]))
    return peaks


def damping_ratio(
    signal: np.ndarray,
    dt: float,
    use_decay: bool = True,
) -> float:
    """
    Estimate damping ratio via logarithmic decrement.

    If use_decay is True, only peak values after the global maximum
    are used (post-release decay envelope). Otherwise all peaks are used.

    Returns zeta (fraction of critical damping).
    """
    peaks = _find_peaks(signal)
    if len(peaks) < 3:
        return 0.0

    if use_decay:
        pk_max_idx = int(np.argmax(peaks))
        peaks = peaks[pk_max_idx:]

    if len(peaks) < 3:
        return 0.0

    deltas = []
    for i in range(len(peaks) - 1):
        if peaks[i] > 1e-6 and peaks[i + 1] > 1e-6:
            deltas.append(np.log(peaks[i] / peaks[i + 1]))

    if not deltas:
        return 0.0

    delta = float(np.mean(deltas))
    zeta = delta / np.sqrt(4.0 * np.pi**2 + delta**2)
    return zeta


def extract_from_release(
    gmag: np.ndarray,
    dt: float,
    peak_idx: int,
    window_samples: int = 80,
) -> Tuple[float, float]:
    """
    Extract natural frequency and damping ratio from a release event.

    Takes gyro magnitude array, sample interval, the index of the
    release spike peak, and the number of post-peak samples to analyse.

    Returns (fn_hz, zeta).
    """
    start = peak_idx
    end = min(len(gmag), peak_idx + window_samples)
    if end - start < 8:
        return 0.0, 0.0

    segment = gmag[start:end]
    fn = natural_frequency(segment, dt)
    zeta = damping_ratio(segment, dt, use_decay=True)
    return fn, zeta


def sway_amplitude(gmag: np.ndarray) -> float:
    """RMS sway amplitude (std of gyro magnitude)."""
    return float(np.std(gmag))


def tilt_angles(
    ax: float,
    ay: float,
    az: float,
) -> Tuple[float, float]:
    """
    Compute static tilt angles from accelerometer readings.

    Returns (tilt_x_deg, tilt_y_deg) using small-angle approximation.
    """
    tx = float(np.degrees(np.arcsin(np.clip(ax, -1.0, 1.0))))
    ty = float(np.degrees(np.arcsin(np.clip(ay, -1.0, 1.0))))
    return tx, ty
