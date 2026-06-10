"""Parameter extraction for detected disturbance events.

Plane 2 (offline): natural frequency (FFT, ZC, PK), sway amplitude,
tilt angles (atan2), and the unified Pipeline class.

No legacy gmag FFT, log-decrement damping, or RMS sway.
"""

from dataclasses import dataclass, field
from typing import List, Optional, Tuple

import numpy as np

from ._calibration import calibrate, BIAS_GX, BIAS_GY, BIAS_GZ
from ._detection import (
    Event,
    EventDetector,
    State,
    classify_event,
    is_dynamic,
    tkeo_streaming,
)
from ._envelope import (
    DecayQuality,
    damping_from_envelope,
    envelope_peak_hold,
    find_decay_onset_tkeo,
)
from ._io import load_imu
from ._ringbuffer import RingBuffer


# -- Natural frequency --


def extract_natural_frequency(segment: np.ndarray, dt: float) -> float:
    """Estimate dominant natural frequency via FFT on signed axis.

    Designed for signed dominant-axis gyro data (not rectified gmag).
    Uses the actual segment length (no forced padding). Applies a Hann
    window and scans bins corresponding to 0.5-25 Hz.
    """
    n = len(segment)
    if n < 4:
        return 0.0

    detrended = segment - np.mean(segment)
    windowed = detrended * np.hanning(n)

    fft = np.abs(np.fft.rfft(windowed))
    freqs = np.fft.rfftfreq(n, d=dt)

    if len(fft) <= 1:
        return 0.0

    lo_bin = max(1, int(0.5 * n * dt))
    hi_bin = min(len(fft) - 1, int(25.0 * n * dt) + 1)

    if lo_bin >= len(fft) or lo_bin >= hi_bin:
        return 0.0

    band = fft[lo_bin:hi_bin + 1]
    peak_rel = int(np.argmax(band))
    return float(freqs[lo_bin + peak_rel])


def extract_frequency_zc(
    signed_axis_seg: np.ndarray,
    dt: float,
) -> float:
    """Estimate natural frequency from zero-crossing periods.

    Detrends the signed axis, finds zero-crossings, computes full-cycle
    periods (2 zero-crossings per cycle), and returns 1/mean(period).

    Cheapest method (O(n), no FFT/windowing). Recommended ESP32 candidate.
    """
    n = len(signed_axis_seg)
    if n < 3:
        return 0.0

    detrended = signed_axis_seg - np.mean(signed_axis_seg)

    signs = np.sign(detrended)
    crossings = []
    for i in range(n - 1):
        if signs[i] != signs[i + 1]:
            crossings.append(i + 1)

    if len(crossings) < 4:
        return 0.0

    periods = []
    for k in range(0, len(crossings) - 2, 2):
        period = (crossings[k + 2] - crossings[k]) * dt
        if period > 0:
            periods.append(period)

    if not periods:
        return 0.0

    mean_period = float(np.mean(periods))
    return 1.0 / mean_period if mean_period > 0 else 0.0


def extract_frequency_pk(
    signed_axis_seg: np.ndarray,
    dt: float,
) -> float:
    """Estimate natural frequency from peak-to-peak periods.

    Finds local maxima on the signed axis, computes inter-peak periods,
    excludes outliers (>3sigma from median), and returns 1/mean(period).
    """
    n = len(signed_axis_seg)
    if n < 5:
        return 0.0

    peaks = []
    for i in range(1, n - 1):
        if signed_axis_seg[i] > signed_axis_seg[i - 1] and \
           signed_axis_seg[i] > signed_axis_seg[i + 1]:
            peaks.append(i)

    if len(peaks) < 3:
        return 0.0

    periods = []
    for i in range(len(peaks) - 1):
        period = (peaks[i + 1] - peaks[i]) * dt
        if period > 0:
            periods.append(period)

    if len(periods) < 2:
        return 0.0

    median_period = float(np.median(periods))
    std_period = float(np.std(periods))
    filtered = [p for p in periods if abs(p - median_period) <= 3.0 * std_period]

    if not filtered:
        return 0.0

    mean_period = float(np.mean(filtered))
    return 1.0 / mean_period if mean_period > 0 else 0.0


# -- Active region / sway --


def extract_active_region(
    segment: np.ndarray,
    peak_gmag: float,
    ratio: float | None = None,
    threshold: float | None = None,
    baseline: float = 0.35,
) -> Tuple[int, int]:
    """Identify the active sub-segment where branch motion exceeds threshold.

    Threshold priority:
    1. If `threshold` is provided: use it directly.
    2. Elif `ratio` is provided (DEPRECATED): threshold = ratio * peak_gmag.
    3. Else (default): energy-adaptive RMS-based threshold.

    The RMS-based default works for all disturbance types without tuning:
        RMS = sqrt(mean(segment^2))
        threshold = max(baseline + 3.0, RMS / 3.0)
    """
    n = len(segment)
    if n < 3:
        return 0, n - 1

    if threshold is not None:
        thr = threshold
    elif ratio is not None:
        thr = ratio * peak_gmag
    else:
        rms = float(np.sqrt(np.mean(segment ** 2)))
        thr = max(baseline + 3.0, rms / 3.0)

    above = np.where(segment > thr)[0]

    if len(above) < 3:
        return 0, n - 1

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
    """
    n_active = active_end - active_start + 1
    if n_active < 3:
        return 0.0, 0.0, 0.0

    def _axis_sway(axis_seg: np.ndarray) -> float:
        active = axis_seg[active_start:active_end + 1]
        if active.max() - active.min() < 1e-6:
            return 0.0
        angle = np.cumsum(active) * dt
        return float(angle.max() - angle.min())

    return (
        _axis_sway(gyro_x_seg),
        _axis_sway(gyro_y_seg),
        _axis_sway(gyro_z_seg),
    )


# -- Tilt (atan2) --


def extract_tilt(
    ax_segment: np.ndarray,
    ay_segment: np.ndarray,
    az_segment: np.ndarray,
) -> Tuple[float, float]:
    """Compute static tilt angles from accelerometer readings via atan2.

    Uses atan2 decomposition of the gravity vector instead of arcsin,
    which correctly handles dual-axis tilt and the full angle range.

    Args:
        ax_segment, ay_segment, az_segment: Calibrated accel segments.

    Returns:
        (tilt_x_deg, tilt_y_deg).
    """
    ax_mean = float(np.mean(ax_segment))
    ay_mean = float(np.mean(ay_segment))
    az_mean = float(np.mean(az_segment))

    tilt_x = float(np.degrees(np.arctan2(ax_mean, np.sqrt(ay_mean**2 + az_mean**2))))
    tilt_y = float(np.degrees(np.arctan2(ay_mean, np.sqrt(ax_mean**2 + az_mean**2))))

    return tilt_x, tilt_y


# -- Event result --


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
    damping_confidence: str
    sway_x_deg: float
    sway_y_deg: float
    sway_z_deg: float
    tilt_x_deg: float
    tilt_y_deg: float
    is_truncated: bool
    gmag_segment: np.ndarray = field(repr=False)
    ax_segment: np.ndarray = field(repr=False)
    ay_segment: np.ndarray = field(repr=False)
    az_segment: np.ndarray = field(repr=False)
    gx_segment: np.ndarray = field(repr=False, default_factory=lambda: np.array([], dtype=np.float32))
    gy_segment: np.ndarray = field(repr=False, default_factory=lambda: np.array([], dtype=np.float32))
    gz_segment: np.ndarray = field(repr=False, default_factory=lambda: np.array([], dtype=np.float32))

    # Additional fields added by the new Pipeline
    fn_method: str = ""
    fn_zc_hz: float = 0.0
    fn_pk_hz: float = 0.0
    damping_confidence_level: str = ""
    decay_onset_time_s: float = 0.0
    decay_quality: str = ""
    fit_samples: int = 0
    fit_cycles: float = 0.0
    amplitude_drop: float = 0.0
    r_squared: float = 0.0
    samples_per_cycle: float = 0.0


# -- Pipeline --


class Pipeline:
    """Composes detection + ring buffer + extraction into one pipeline.

    Usage:
        pipe = Pipeline()
        results = pipe.process_csv("raw_log_7.csv")
    """

    def __init__(
        self,
        hi_thresh: float = 40.0,
        lo_thresh: float = 5.0,
        gmag_onset: float = 2.0,
        gmag_thresh: float = 1.5,
        min_quiet: int = 52,
        ring_size: int = 2048,
        dt: float = None,
    ):
        self.detector = EventDetector(
            hi_thresh=hi_thresh,
            lo_thresh=lo_thresh,
            gmag_onset=gmag_onset,
            gmag_thresh=gmag_thresh,
            min_quiet=min_quiet,
        )
        self.ring = RingBuffer(size=ring_size)
        self.dt = dt

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

        Calibrate -> gmag -> write ring -> compute TKEO -> state machine.
        """
        gx_c, gy_c, gz_c, _, _, _ = calibrate(gx, gy, gz, ax, ay, az)
        gmag_val = float(np.sqrt(gx_c ** 2 + gy_c ** 2 + gz_c ** 2))

        self.ring.write(gmag_val)

        tkeo_val = self._compute_tkeo(gmag_val)

        return self.detector.process_sample(tkeo_val, gmag_val)

    def _compute_tkeo(self, gmag_val: float) -> float:
        """Compute TKEO from ring buffer history.

        psi[n-1] = x[n-1]^2 - x[n-2]*x[n]
        """
        wp = self.ring.write_ptr
        if wp < 3:
            return gmag_val

        idx_n1 = (wp - 2) & self.ring.MASK
        idx_n2 = (wp - 3) & self.ring.MASK
        x_n1 = float(self.ring._buf[idx_n1])
        x_n2 = float(self.ring._buf[idx_n2])

        return tkeo_streaming(x_n1, x_n2, gmag_val)

    def _extract_params(
        self,
        event: Event,
        gmag_segment: np.ndarray | None = None,
    ) -> EventResult:
        """Run offline plane: extract all parameters for one event."""
        onset_age = event.offset_write_ptr - event.onset_idx
        truncated = onset_age > self.ring.SIZE

        if gmag_segment is not None:
            gmag_seg = gmag_segment.astype(np.float32)
        else:
            seg_len = min(onset_age, self.ring.SIZE)
            gmag_seg = self.ring.read_segment(event.onset_idx, seg_len)

        event_type = classify_event(event.peak_gmag, event.dur_samples)

        fn = 0.0

        return EventResult(
            event_type=event_type,
            onset_idx=event.onset_idx,
            offset_idx=event.offset_idx,
            dur_samples=event.dur_samples,
            peak_gmag=event.peak_gmag,
            natural_frequency_hz=fn,
            damping_ratio=0.0,
            damping_confidence="low",
            sway_x_deg=0.0,
            sway_y_deg=0.0,
            sway_z_deg=0.0,
            tilt_x_deg=0.0,
            tilt_y_deg=0.0,
            is_truncated=truncated,
            gmag_segment=gmag_seg,
            ax_segment=np.array([], dtype=np.float32),
            ay_segment=np.array([], dtype=np.float32),
            az_segment=np.array([], dtype=np.float32),
        )

    def process_csv(self, filepath: str, verbose: bool = False) -> List[EventResult]:
        """Full batch processing of a raw IMU CSV file.

        Auto-detects dt and baseline_gmag. Runs detection pass, then
        extraction per event with:
          - TKEO energy-burst decay onset
          - Dominant axis selection (largest sway)
          - Signed-axis FFT for fn
          - gmag peak-hold envelope + bounded log-fit damping
          - Three-way fn comparison (FFT, ZC, PK) with warning on >20% mismatch
        """
        import pandas as pd

        cols = [
            "timestamp_us", "accel_x", "accel_y", "accel_z",
            "gyro_x", "gyro_y", "gyro_z", "temp_c",
        ]
        df = pd.read_csv(filepath, header=None, names=cols)

        if self.dt is None:
            ts = (df["timestamp_us"] - df["timestamp_us"].iloc[0]) / 1e6
            self.dt = float(np.median(np.diff(ts)))

        n_baseline = min(250, max(50, int(5.0 / self.dt)))
        gx_bl = df["gyro_x"].values[:n_baseline] - BIAS_GX
        gy_bl = df["gyro_y"].values[:n_baseline] - BIAS_GY
        gz_bl = df["gyro_z"].values[:n_baseline] - BIAS_GZ
        self.baseline_gmag = float(np.mean(np.sqrt(gx_bl**2 + gy_bl**2 + gz_bl**2)))

        gx_cal = df["gyro_x"].values - BIAS_GX
        gy_cal = df["gyro_y"].values - BIAS_GY
        gz_cal = df["gyro_z"].values - BIAS_GZ
        gmag_all = np.sqrt(gx_cal**2 + gy_cal**2 + gz_cal**2)

        self.reset()
        events: List[Event] = []

        for _, row in df.iterrows():
            event = self._process_row(
                row["gyro_x"], row["gyro_y"], row["gyro_z"],
                row["accel_x"], row["accel_y"], row["accel_z"],
            )
            if event is not None:
                events.append(event)

        if self.detector.state == State.ACTIVE:
            wp = self.ring.write_ptr
            event = Event(
                onset_idx=self.detector.onset_idx,
                offset_idx=wp - 1,
                offset_write_ptr=wp,
                peak_gmag=self.detector.peak_gmag,
                dur_samples=self.detector.dur_samples,
            )
            events.append(event)

        results = []
        for event in events:
            onset = event.onset_idx
            offset = event.offset_idx

            if onset < len(df) and offset < len(df):
                gmag_seg = gmag_all[onset:offset + 1]
            else:
                gmag_seg = None

            result = self._extract_params(event, gmag_segment=gmag_seg)

            if onset < len(df) and offset < len(df):
                seg = df.iloc[onset:offset + 1]
                result.ax_segment = seg["accel_x"].values.astype(np.float32)
                result.ay_segment = seg["accel_y"].values.astype(np.float32)
                result.az_segment = seg["accel_z"].values.astype(np.float32)
                result.gx_segment = (seg["gyro_x"].values - BIAS_GX).astype(np.float32)
                result.gy_segment = (seg["gyro_y"].values - BIAS_GY).astype(np.float32)
                result.gz_segment = (seg["gyro_z"].values - BIAS_GZ).astype(np.float32)

                tx, ty = extract_tilt(
                    result.ax_segment, result.ay_segment, result.az_segment,
                )
                result.tilt_x_deg = tx
                result.tilt_y_deg = ty

                if is_dynamic(result.event_type):
                    active_start, active_end = extract_active_region(
                        result.gmag_segment, event.peak_gmag,
                        baseline=self.baseline_gmag,
                    )
                    sx, sy, sz = extract_active_sway(
                        result.gx_segment, result.gy_segment, result.gz_segment,
                        active_start, active_end, self.dt,
                    )
                    result.sway_x_deg = sx
                    result.sway_y_deg = sy
                    result.sway_z_deg = sz

                    decay_onset, decay_quality = find_decay_onset_tkeo(
                        result.gmag_segment,
                        self.dt,
                        baseline_gmag=self.baseline_gmag,
                    )

                    result.decay_quality = decay_quality.name
                    result.decay_onset_time_s = decay_onset * self.dt

                    sway_values = [abs(sx), abs(sy), abs(sz)]
                    max_sway = max(sway_values)
                    if max_sway > 0.0:
                        dominant = int(np.argmax(sway_values))
                        axis_map = {
                            0: result.gx_segment,
                            1: result.gy_segment,
                            2: result.gz_segment,
                        }
                        signed_full_seg = axis_map[dominant]

                        decay_end = len(result.gmag_segment) - 1
                        decay_signed = signed_full_seg[
                            decay_onset:decay_end + 1
                        ]
                        fn_fft_signed = extract_natural_frequency(
                            decay_signed, self.dt,
                        )
                        fn_zc = extract_frequency_zc(decay_signed, self.dt)
                        fn_pk = extract_frequency_pk(decay_signed, self.dt)
                        fn = fn_fft_signed if fn_fft_signed > 0 else fn_zc
                        result.natural_frequency_hz = fn
                        result.fn_method = "fft" if fn_fft_signed > 0 else ("zc" if fn_zc > 0 else ("pk" if fn_pk > 0 else "null"))
                        result.fn_zc_hz = fn_zc
                        result.fn_pk_hz = fn_pk

                        if decay_quality == DecayQuality.NONE:
                            zeta_env, conf_env = 0.0, "low"
                            result.fit_samples = 0
                            result.fit_cycles = 0.0
                            result.amplitude_drop = 0.0
                            result.r_squared = 0.0
                            result.samples_per_cycle = 0.0
                        else:
                            if fn > 0 and len(decay_signed) >= 3:
                                try:
                                    gmag_env = envelope_peak_hold(
                                        result.gmag_segment, self.dt, fc=2.0,
                                    )
                                    decay_env = gmag_env[decay_onset:]
                                    zeta_env, conf_env, diag = damping_from_envelope(
                                        decay_env, self.dt, fn,
                                        baseline_noise=self.baseline_gmag,
                                        skip_transient_cycles=1,
                                        return_diagnostics=True,
                                    )
                                    result.fit_samples = diag.get("fit_sample_count", 0)
                                    result.fit_cycles = diag.get("fit_cycle_count", 0.0)
                                    result.amplitude_drop = diag.get("amplitude_drop", 0.0)
                                    result.r_squared = diag.get("r_squared", 0.0)
                                    result.samples_per_cycle = 1.0 / (fn * self.dt) if fn > 0 and self.dt > 0 else 0.0
                                except (ValueError, Exception):
                                    zeta_env, conf_env = 0.0, "low"
                                    result.fit_samples = 0
                                    result.fit_cycles = 0.0
                                    result.amplitude_drop = 0.0
                                    result.r_squared = 0.0
                                    result.samples_per_cycle = 0.0
                            else:
                                zeta_env, conf_env = 0.0, "low"
                                result.fit_samples = 0
                                result.fit_cycles = 0.0
                                result.amplitude_drop = 0.0
                                result.r_squared = 0.0
                                result.samples_per_cycle = 0.0
                            if decay_quality == DecayQuality.LOW and conf_env != "low":
                                conf_env = "medium"
                        result.damping_ratio = zeta_env
                        result.damping_confidence = conf_env

                        if verbose:
                            dom_name = ["X", "Y", "Z"][dominant]
                            print(
                                f"  fn (signed {dom_name}, decay region "
                                f"[onset={decay_onset}], env zeta={zeta_env:.3f} "
                                f"({conf_env})): "
                                f"FFT={fn_fft_signed:.2f}, "
                                f"ZC={fn_zc:.2f}, "
                                f"PK={fn_pk:.2f} Hz"
                            )
                            if fn_fft_signed > 0 and fn_zc > 0:
                                diff = abs(fn_fft_signed - fn_zc) / fn_fft_signed
                                if diff > 0.20:
                                    print(
                                        f"  WARN: FFT-ZC mismatch: {diff*100:.0f}% "
                                        f"(possible signal quality issue)"
                                    )

            results.append(result)

        return results


def run_pipeline(csv_path: str, verbose: bool = False) -> List[EventResult]:
    """Convenience function to create a Pipeline, process a CSV, and return results."""
    pipe = Pipeline()
    return pipe.process_csv(csv_path, verbose=verbose)
