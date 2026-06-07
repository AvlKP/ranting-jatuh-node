import math
import numpy as np
from scipy import signal


def cheby1_hpf_coeffs(fc_hz, fs_hz, order=2, ripple_db=1.0):
    b, a = signal.cheby1(N=order, rp=ripple_db, Wn=fc_hz,
                         btype='high', fs=fs_hz)
    b = b / a[0]
    a = a / a[0]
    return b, a


def cheby1_hpf_apply(samples, b, a, timestamps=None):
    n = len(samples)
    output = np.zeros(n, dtype=np.float64)

    b0, b1, b2 = float(b[0]), float(b[1]), float(b[2])
    a1, a2 = float(a[1]), float(a[2])

    w1 = 0.0
    w2 = 0.0

    for i in range(n):
        x = samples[i]
        w0 = x - a1 * w1 - a2 * w2
        y = b0 * w0 + b1 * w1 + b2 * w2
        output[i] = y
        w2 = w1
        w1 = w0

    return output


def detect_disturbed(df,
                     hp_cutoff_hz=0.2,
                     hp_threshold_g=0.02,
                     hp_order=2,
                     hp_ripple_db=1.0,
                     hp_fs_hz=None,
                     debounce=64,
                     settle_samples=500):
    n = len(df)
    ts = df['timestamp_us'].to_numpy(dtype=np.float64) / 1e6

    if hp_fs_hz is None:
        dt_vals = np.diff(ts[ts > 0][:min(1000, n)])
        hp_fs_hz = float(1.0 / np.median(dt_vals)) if len(dt_vals) > 0 else 30.0

    b, a = cheby1_hpf_coeffs(hp_cutoff_hz, hp_fs_hz, hp_order, hp_ripple_db)

    hpf_x = cheby1_hpf_apply(df['accel_x'].to_numpy(dtype=np.float64), b, a)
    hpf_y = cheby1_hpf_apply(df['accel_y'].to_numpy(dtype=np.float64), b, a)
    hpf_z = cheby1_hpf_apply(df['accel_z'].to_numpy(dtype=np.float64), b, a)

    hpf_mag = np.sqrt(hpf_x ** 2 + hpf_y ** 2 + hpf_z ** 2)

    state = np.zeros(n, dtype=np.uint8)
    in_disturbed = False
    quiet_counter = 0

    for i in range(n):
        val = hpf_mag[i]
        if in_disturbed:
            if val < hp_threshold_g:
                quiet_counter += 1
                if quiet_counter >= debounce:
                    in_disturbed = False
                    quiet_counter = 0
            else:
                quiet_counter = 0
        else:
            if i >= settle_samples and val > hp_threshold_g:
                in_disturbed = True
                quiet_counter = 0

        state[i] = 1 if in_disturbed else 0

    return state, hpf_x, hpf_y, hpf_z, hpf_mag, hp_threshold_g, hp_fs_hz
