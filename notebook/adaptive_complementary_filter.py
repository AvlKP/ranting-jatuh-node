import math
import numpy as np


def adaptive_complementary_filter(accel, gyro, dt,
                                   alpha_base=0.98,
                                   k_gain=50.0,
                                   pitch_rad=0.0,
                                   roll_rad=0.0):
    gx_rad = math.radians(gyro[0])
    gy_rad = math.radians(gyro[1])

    accel_pitch = math.atan2(accel[0], accel[2])
    accel_roll = math.atan2(-accel[1], accel[2])

    gyro_pitch = pitch_rad + gy_rad * dt
    gyro_roll = roll_rad + gx_rad * dt

    accel_mag = math.sqrt(accel[0] ** 2 + accel[1] ** 2 + accel[2] ** 2)
    error = abs(accel_mag - 1.0)
    weight = 1.0 / (1.0 + k_gain * error)
    alpha = 1.0 - (1.0 - alpha_base) * weight

    pitch_rad = alpha * gyro_pitch + (1.0 - alpha) * accel_pitch
    roll_rad = alpha * gyro_roll + (1.0 - alpha) * accel_roll

    pitch_deg = math.degrees(pitch_rad)
    roll_deg = math.degrees(roll_rad)

    return pitch_deg, roll_deg, pitch_rad, roll_rad


def apply_adaptive_complementary_filter(df, alpha_base=0.98, k_gain=50.0):
    timestamps = df['timestamp_us'].to_numpy(dtype=np.float64)
    accel_x = df['accel_x'].to_numpy(dtype=np.float64)
    accel_y = df['accel_y'].to_numpy(dtype=np.float64)
    accel_z = df['accel_z'].to_numpy(dtype=np.float64)
    gyro_x = df['gyro_x'].to_numpy(dtype=np.float64)
    gyro_y = df['gyro_y'].to_numpy(dtype=np.float64)

    n = len(df)
    pitch_deg = np.empty(n, dtype=np.float64)
    roll_deg = np.empty(n, dtype=np.float64)
    alpha_hist = np.empty(n, dtype=np.float64)
    pitch_rad = 0.0
    roll_rad = 0.0

    for i in range(n):
        dt = ((timestamps[i] - timestamps[0]) / 1e6 if i == 0
              else (timestamps[i] - timestamps[i - 1]) / 1e6)

        pitch_d, roll_d, pitch_rad, roll_rad = adaptive_complementary_filter(
            [accel_x[i], accel_y[i], accel_z[i]],
            [gyro_x[i], gyro_y[i], 0.0],
            dt,
            alpha_base,
            k_gain,
            pitch_rad,
            roll_rad,
        )

        pitch_deg[i] = pitch_d
        roll_deg[i] = roll_d

        accel_mag = math.sqrt(accel_x[i]**2 + accel_y[i]**2 + accel_z[i]**2)
        err = abs(accel_mag - 1.0)
        w = 1.0 / (1.0 + k_gain * err)
        alpha_hist[i] = 1.0 - (1.0 - alpha_base) * w

    df = df.copy()
    df['pitch_adapt'] = pitch_deg
    df['roll_adapt'] = roll_deg
    df['alpha_adapt'] = alpha_hist
    return df
