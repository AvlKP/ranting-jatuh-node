#!/usr/bin/env python3
"""Parse and analyze monitor debug dump files from SD card.

Reads /sdcard/dbg_dump.csv, parses snapshots delimited by >>>SNAPSHOT/<<<END,
and provides --plot (matplotlib) and --json (structured analysis) output modes.
"""

import argparse
import dataclasses
import json
import math
import sys
from typing import List


@dataclasses.dataclass
class DebugSnapshot:
    index: int
    timestamp_us: int
    sample_count: int
    rate_hz: float
    roll_decay_start: int
    roll_decay_count: int
    pitch_decay_start: int
    pitch_decay_count: int
    esp_freq_roll_hz: float
    esp_zeta_roll: float
    esp_freq_pitch_hz: float
    esp_zeta_pitch: float
    roll_peaks_amps: List[float]
    roll_peaks_times: List[float]
    pitch_peaks_amps: List[float]
    pitch_peaks_times: List[float]
    raw_roll: List[float]
    raw_pitch: List[float]


def parse_dump(filepath: str) -> List[DebugSnapshot]:
    snapshots: List[DebugSnapshot] = []
    idx = 0

    with open(filepath, "r") as f:
        lines = iter(f)
        for line in lines:
            line = line.strip()
            if line == ">>>SNAPSHOT":
                snap = _parse_single_snapshot(lines, idx)
                if snap is not None:
                    snapshots.append(snap)
                idx += 1

    return snapshots


def _parse_single_snapshot(lines, idx: int) -> Optional[DebugSnapshot]:
    ts = 0
    samples = 0
    rate = 0.0
    r_decay_start = 0
    r_decay_count = 0
    p_decay_start = 0
    p_decay_count = 0
    efreq_r = 0.0
    ezeta_r = 0.0
    efreq_p = 0.0
    ezeta_p = 0.0
    r_amps: List[float] = []
    r_times: List[float] = []
    p_amps: List[float] = []
    p_times: List[float] = []
    raw_r: List[float] = []
    raw_p: List[float] = []

    for line in lines:
        line = line.strip()
        if line == "<<<END" or not line:
            if line == "<<<END":
                break
            continue

        tag, _, rest = line.partition(",")
        parts = rest.split(",") if rest else []

        if tag == "META":
            ts = int(parts[0])
            samples = int(parts[1])
            rate = float(parts[2])
        elif tag == "DECAY":
            axis = parts[0].strip()
            start = int(parts[1])
            count = int(parts[2])
            if axis == "R":
                r_decay_start, r_decay_count = start, count
            elif axis == "P":
                p_decay_start, p_decay_count = start, count
        elif tag == "RESULT":
            axis = parts[0].strip()
            freq = float(parts[1])
            zeta = float(parts[2])
            if axis == "R":
                efreq_r, ezeta_r = freq, zeta
            elif axis == "P":
                efreq_p, ezeta_p = freq, zeta
        elif tag == "PEAKS":
            axis = parts[0].strip()
            n = int(parts[1])
            amps = []
            times = []
            for i in range(n):
                amps.append(float(parts[2 + 2 * i]))
                times.append(float(parts[3 + 2 * i]))
            if axis == "R":
                r_amps, r_times = amps, times
            elif axis == "P":
                p_amps, p_times = amps, times
        elif tag == "RAW":
            axis = parts[0].strip()
            vals = [float(v) for v in parts[1:]]
            if axis == "R":
                raw_r = vals
            elif axis == "P":
                raw_p = vals

    return DebugSnapshot(
        index=idx,
        timestamp_us=ts,
        sample_count=samples,
        rate_hz=rate,
        roll_decay_start=r_decay_start,
        roll_decay_count=r_decay_count,
        pitch_decay_start=p_decay_start,
        pitch_decay_count=p_decay_count,
        esp_freq_roll_hz=efreq_r,
        esp_zeta_roll=ezeta_r,
        esp_freq_pitch_hz=efreq_p,
        esp_zeta_pitch=ezeta_p,
        roll_peaks_amps=r_amps,
        roll_peaks_times=r_times,
        pitch_peaks_amps=p_amps,
        pitch_peaks_times=p_times,
        raw_roll=raw_r,
        raw_pitch=raw_p,
    )


def recompute_frequency(raw_data: List[float], decay_start: int,
                        decay_count: int, rate_hz: float) -> float:
    return _welch_peak_freq(raw_data, rate_hz)


def recompute_damping(peaks_amps: List[float], peaks_times: List[float],
                      natural_freq_hz: float) -> float:
    if len(peaks_amps) < 4 or natural_freq_hz <= 0.0:
        return 0.0

    wn = 2.0 * math.pi * natural_freq_hz
    n = len(peaks_amps)

    sum_t = 0.0
    sum_y = 0.0
    sum_ty = 0.0
    sum_t2 = 0.0

    for i in range(n):
        t = peaks_times[i]
        y = math.log(peaks_amps[i])
        sum_t += t
        sum_y += y
        sum_ty += t * y
        sum_t2 += t * t

    fn = float(n)
    denominator = (fn * sum_t2) - (sum_t * sum_t)

    if denominator <= 0.0:
        return 0.0

    slope = ((fn * sum_ty) - (sum_t * sum_y)) / denominator

    if slope >= 0.0:
        return 0.0

    return abs(slope) / wn


def _welch_peak_freq(data: List[float], rate_hz: float) -> float:
    if not data or len(data) < 2:
        return 0.0

    import numpy as np
    try:
        from scipy.signal import welch
    except ImportError:
        print("ERROR: scipy required for frequency recomputation. pip install scipy",
              file=sys.stderr)
        return 0.0

    region = np.array(data, dtype=np.float64)
    region = region - np.mean(region)

    window = 0.5 - 0.5 * np.cos(2.0 * np.pi * np.arange(len(region)) / (len(region) - 1))
    region = region * window

    n = len(region)
    fft_size = 512 if n < 1024 else 1024
    nperseg = min(fft_size, n)
    noverlap = nperseg // 2

    freqs, psd = welch(region, fs=rate_hz, nperseg=nperseg,
                       noverlap=noverlap, window="boxcar",
                       detrend=False, scaling="spectrum")

    valid_idx = np.where(freqs >= 0.5)[0]
    if len(valid_idx) == 0:
        return 0.0

    peak_idx = valid_idx[np.argmax(psd[valid_idx])]
    return float(freqs[peak_idx])


def generate_plot(snapshot: DebugSnapshot, output_file: Optional[str] = None):
    import numpy as np

    try:
        import matplotlib
        if output_file:
            matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("ERROR: matplotlib required for --plot. pip install matplotlib",
              file=sys.stderr)
        return

    try:
        from scipy.signal import welch
    except ImportError:
        print("ERROR: scipy required for --plot. pip install scipy",
              file=sys.stderr)
        return

    rate = snapshot.rate_hz
    raw_r = np.array(snapshot.raw_roll, dtype=np.float64)
    raw_p = np.array(snapshot.raw_pitch, dtype=np.float64)
    t = np.arange(len(raw_r)) / rate

    py_freq_r = recompute_frequency(list(raw_r), snapshot.roll_decay_start,
                                    snapshot.roll_decay_count, rate)
    py_freq_p = recompute_frequency(list(raw_p), snapshot.pitch_decay_start,
                                    snapshot.pitch_decay_count, rate)
    py_zeta_r = recompute_damping(snapshot.roll_peaks_amps, snapshot.roll_peaks_times,
                                  py_freq_r)
    py_zeta_p = recompute_damping(snapshot.pitch_peaks_amps, snapshot.pitch_peaks_times,
                                  py_freq_p)

    fig, axes = plt.subplots(2, 2, figsize=(14, 10))
    fig.suptitle(f"Snapshot {snapshot.index} — Timestamp {snapshot.timestamp_us} us",
                 fontweight="bold")

    ax0 = axes[0, 0]
    ax0.plot(t, raw_r, "b-", alpha=0.5, linewidth=0.5, label="Roll")
    ax0.plot(t, raw_p, "r-", alpha=0.5, linewidth=0.5, label="Pitch")
    _draw_decay_spans(ax0, snapshot, t)
    ax0.set_xlabel("Time (s)")
    ax0.set_ylabel("Tilt (deg)")
    ax0.set_title("Raw Signal + Decay Regions")
    ax0.legend(fontsize=7)
    ax0.grid(True, alpha=0.3)

    ax1 = axes[0, 1]
    _draw_psd(ax1, snapshot, raw_r, raw_p, rate, welch)
    ax1.set_title("Welch PSD — Decay Region")
    ax1.legend(fontsize=7)

    ax2 = axes[1, 0]
    _draw_peak_envelope(ax2, snapshot, py_freq_r, py_freq_p, py_zeta_r, py_zeta_p)

    ax3 = axes[1, 1]
    _draw_comparison_table_basic(ax3, snapshot, py_freq_r, py_freq_p,
                                 py_zeta_r, py_zeta_p)

    plt.tight_layout()
    if output_file:
        plt.savefig(output_file, dpi=150, bbox_inches="tight")
        print(f"Plot saved to {output_file}")
    else:
        plt.show()
    plt.close(fig)


def _draw_decay_spans(ax, snapshot, t):
    if snapshot.roll_decay_count > 0:
        r0 = snapshot.roll_decay_start
        r1 = r0 + snapshot.roll_decay_count
        if r1 > len(t):
            r1 = len(t)
        ax.axvspan(t[r0], t[r1 - 1], alpha=0.15, color="blue", label="Roll decay")
    if snapshot.pitch_decay_count > 0:
        p0 = snapshot.pitch_decay_start
        p1 = p0 + snapshot.pitch_decay_count
        if p1 > len(t):
            p1 = len(t)
        ax.axvspan(t[p0], t[p1 - 1], alpha=0.15, color="red", label="Pitch decay")


def _draw_psd(ax, snapshot, raw_r, raw_p, rate, welch):
    import numpy as np

    for axis_name, raw, decay_start, decay_count, color, label in [
        ("Roll", raw_r, snapshot.roll_decay_start, snapshot.roll_decay_count, "blue", "Roll"),
        ("Pitch", raw_p, snapshot.pitch_decay_start, snapshot.pitch_decay_count, "red", "Pitch"),
    ]:
        if decay_count < 2:
            continue
        region = raw
        n = len(region)
        if n < 2:
            continue
        region = region - np.mean(region)
        window = 0.5 - 0.5 * np.cos(2.0 * np.pi * np.arange(n) / (n - 1))
        region = region * window
        fft_sz = 512 if n < 1024 else 1024
        nperseg = min(fft_sz, n)
        noverlap = nperseg // 2
        freqs, psd = welch(region, fs=rate, nperseg=nperseg,
                           noverlap=noverlap, window="boxcar",
                           detrend=False, scaling="spectrum")
        if len(freqs) < 2:
            continue

        valid_idx = np.where(freqs >= 0.5)[0]
        if len(valid_idx) > 0:
            peak_idx = valid_idx[np.argmax(psd[valid_idx])]
        else:
            peak_idx = int(np.argmax(psd[1:])) + 1
        ax.semilogy(freqs, psd, color=color, alpha=0.7, linewidth=0.8, label=label)
        ax.axvline(freqs[peak_idx], color=color, linestyle="--", alpha=0.6,
                    label=f"{label} peak={freqs[peak_idx]:.3f} Hz")

    ax.set_xlabel("Frequency (Hz)")
    ax.set_ylabel("PSD")
    ax.grid(True, alpha=0.3)
    ax.set_xlim(left=0)


def _draw_peak_envelope(ax, snapshot, py_freq_r, py_freq_p, py_zeta_r, py_zeta_p):
    import numpy as np

    for axis_name, amps, times, py_freq, py_zeta, color, label in [
        ("Roll", snapshot.roll_peaks_amps, snapshot.roll_peaks_times,
         py_freq_r, py_zeta_r, "blue", "Roll"),
        ("Pitch", snapshot.pitch_peaks_amps, snapshot.pitch_peaks_times,
         py_freq_p, py_zeta_p, "red", "Pitch"),
    ]:
        if len(amps) < 2:
            continue
        ax.semilogy(times, amps, "o-", color=color, markersize=4, linewidth=0.8, label=f"{label} peaks")

        if len(amps) >= 4 and py_freq > 0 and py_zeta > 0:
            t_pts = np.array(times)
            log_amps = np.log(amps)
            slope, intercept = np.polyfit(t_pts, log_amps, 1)
            t_fit = np.linspace(t_pts[0], t_pts[-1], 100)
            fit_line = np.exp(intercept + slope * t_fit)
            ax.semilogy(t_fit, fit_line, "--", color=color, linewidth=1.0,
                        label=f"{label} fit (zeta={py_zeta:.4f})")

    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Peak Amplitude")
    ax.set_title("Peak Envelope + Log-Linear Regression")
    ax.legend(fontsize=7)
    ax.grid(True, alpha=0.3)


def _make_freq_match(fw, py):
    return "YES" if abs(fw - py) < 0.001 else "NO"


def _draw_comparison_table_basic(ax, snapshot, py_freq_r, py_freq_p,
                                  py_zeta_r, py_zeta_p):
    ax.axis("off")
    table_data = [
        ["Parameter", "ESP32", "Python", "Match?"],
        ["Freq Roll (Hz)",
         f"{snapshot.esp_freq_roll_hz:.4f}",
         f"{py_freq_r:.4f}",
         _make_freq_match(snapshot.esp_freq_roll_hz, py_freq_r)],
        ["Zeta Roll",
         f"{snapshot.esp_zeta_roll:.4f}",
         f"{py_zeta_r:.4f}",
         _make_freq_match(snapshot.esp_zeta_roll, py_zeta_r)],
        ["Freq Pitch (Hz)",
         f"{snapshot.esp_freq_pitch_hz:.4f}",
         f"{py_freq_p:.4f}",
         _make_freq_match(snapshot.esp_freq_pitch_hz, py_freq_p)],
        ["Zeta Pitch",
         f"{snapshot.esp_zeta_pitch:.4f}",
         f"{py_zeta_p:.4f}",
         _make_freq_match(snapshot.esp_zeta_pitch, py_zeta_p)],
        ["Decay Roll (start/count)",
         f"{snapshot.roll_decay_start}/{snapshot.roll_decay_count}",
         "", ""],
        ["Decay Pitch (start/count)",
         f"{snapshot.pitch_decay_start}/{snapshot.pitch_decay_count}",
         "", ""],
        ["Total Samples", f"{snapshot.sample_count}", "", ""],
    ]
    _render_table(ax, table_data)


def _render_table(ax, table_data):
    table = ax.table(cellText=table_data, cellLoc="center", loc="center")
    table.auto_set_font_size(False)
    table.set_fontsize(7)
    table.scale(1.0, 1.3)
    ax.set_title("ESP32 vs Python Comparison", fontweight="bold")


def generate_json(snapshots: List[DebugSnapshot]) -> str:
    results = []
    for snap in snapshots:
        py_freq_r = recompute_frequency(
            snap.raw_roll, snap.roll_decay_start,
            snap.roll_decay_count, snap.rate_hz)
        py_freq_p = recompute_frequency(
            snap.raw_pitch, snap.pitch_decay_start,
            snap.pitch_decay_count, snap.rate_hz)
        py_zeta_r = recompute_damping(
            snap.roll_peaks_amps, snap.roll_peaks_times, py_freq_r)
        py_zeta_p = recompute_damping(
            snap.pitch_peaks_amps, snap.pitch_peaks_times, py_freq_p)

        freq_r_match = abs(snap.esp_freq_roll_hz - py_freq_r) < 0.001
        zeta_r_match = abs(snap.esp_zeta_roll - py_zeta_r) < 0.001
        freq_p_match = abs(snap.esp_freq_pitch_hz - py_freq_p) < 0.001
        zeta_p_match = abs(snap.esp_zeta_pitch - py_zeta_p) < 0.001

        diagnoses = _build_diagnoses(snap, py_freq_r, py_freq_p)

        results.append({
            "snapshot_index": snap.index,
            "timestamp_us": snap.timestamp_us,
            "sample_count": snap.sample_count,
            "rate_hz": snap.rate_hz,
            "roll": {
                "decay": {
                    "start_index": snap.roll_decay_start,
                    "count": snap.roll_decay_count,
                    "duration_s": snap.roll_decay_count / snap.rate_hz if snap.rate_hz > 0 else 0,
                },
                "esp32": {
                    "freq_hz": snap.esp_freq_roll_hz,
                    "zeta": snap.esp_zeta_roll,
                },
                "python": {
                    "freq_hz": py_freq_r,
                    "zeta": py_zeta_r,
                },
                "match": {
                    "freq": freq_r_match,
                    "zeta": zeta_r_match,
                },
                "peak_count": len(snap.roll_peaks_amps),
            },
            "pitch": {
                "decay": {
                    "start_index": snap.pitch_decay_start,
                    "count": snap.pitch_decay_count,
                    "duration_s": snap.pitch_decay_count / snap.rate_hz if snap.rate_hz > 0 else 0,
                },
                "esp32": {
                    "freq_hz": snap.esp_freq_pitch_hz,
                    "zeta": snap.esp_zeta_pitch,
                },
                "python": {
                    "freq_hz": py_freq_p,
                    "zeta": py_zeta_p,
                },
                "match": {
                    "freq": freq_p_match,
                    "zeta": zeta_p_match,
                },
                "peak_count": len(snap.pitch_peaks_amps),
            },
            "diagnoses": diagnoses,
        })

    return json.dumps(results, indent=2, default=str)


def _build_diagnoses(snap: DebugSnapshot, py_freq_r: float,
                     py_freq_p: float) -> List[str]:
    diagnoses: List[str] = []

    if snap.roll_decay_count < 4:
        diagnoses.append("ROLL: decay region too short (<4 samples) — cannot compute reliable frequency")
    if snap.pitch_decay_count < 4:
        diagnoses.append("PITCH: decay region too short (<4 samples) — cannot compute reliable frequency")

    if len(snap.roll_peaks_amps) < 4:
        diagnoses.append("ROLL: fewer than 4 peaks detected — damping regression unreliable")
    if len(snap.pitch_peaks_amps) < 4:
        diagnoses.append("PITCH: fewer than 4 peaks detected — damping regression unreliable")

    if snap.roll_decay_count > 0 and py_freq_r > 0:
        if py_freq_r < 0.1:
            diagnoses.append(
                "ROLL: Python-recomputed frequency <0.1 Hz — likely DC/baseline leakage; "
                "verify de-meaning and zero-padding length")
        elif abs(snap.esp_freq_roll_hz - py_freq_r) > 0.5:
            diagnoses.append(
                "ROLL: ESP32 vs Python frequency mismatch >0.5 Hz — "
                "possible FFT windowing or Welch averaging difference")

    if snap.pitch_decay_count > 0 and py_freq_p > 0:
        if py_freq_p < 0.1:
            diagnoses.append(
                "PITCH: Python-recomputed frequency <0.1 Hz — likely DC/baseline leakage; "
                "verify de-meaning and zero-padding length")
        elif abs(snap.esp_freq_pitch_hz - py_freq_p) > 0.5:
            diagnoses.append(
                "PITCH: ESP32 vs Python frequency mismatch >0.5 Hz — "
                "possible FFT windowing or Welch averaging difference")

    if not diagnoses:
        diagnoses.append("No issues detected — results appear consistent within tolerance")

    return diagnoses


def main():
    parser = argparse.ArgumentParser(
        description="Analyze monitor debug dump from SD card.")
    parser.add_argument("dump_file", nargs="?", default=None,
                        help="Path to dbg_dump.csv from SD card")
    parser.add_argument("--plot", action="store_true",
                        help="Generate matplotlib plots for each snapshot")
    parser.add_argument("--json", dest="json_out", action="store_true",
                        help="Output JSON analysis to stdout")
    parser.add_argument("--snapshot", type=int, default=None,
                        help="Analyze only a specific snapshot index (0-based)")
    parser.add_argument("--output", type=str, default=None,
                        help="Output file for --plot (saves to file instead of showing)")

    args = parser.parse_args()

    if args.dump_file is None:
        parser.print_help()
        sys.exit(1)

    snapshots = parse_dump(args.dump_file)

    if args.snapshot is not None:
        filtered = [s for s in snapshots if s.index == args.snapshot]
        if not filtered:
            print(f"ERROR: snapshot {args.snapshot} not found (have {len(snapshots)} snapshots)",
                  file=sys.stderr)
            sys.exit(1)
        snapshots = filtered

    if args.json_out:
        print(generate_json(snapshots))
    elif args.plot:
        for snap in snapshots:
            out_file = None
            if args.output and len(snapshots) > 1:
                base, _, ext = args.output.rpartition(".")
                out_file = f"{base}_{snap.index}.{ext}" if ext else f"{args.output}_{snap.index}"
            elif args.output:
                out_file = args.output
            generate_plot(snap, output_file=out_file)
    else:
        print(f"Parsed {len(snapshots)} snapshot(s). Use --plot or --json for analysis.")


if __name__ == "__main__":
    main()
