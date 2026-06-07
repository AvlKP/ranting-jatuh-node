import math
import numpy as np
import pandas as pd


def complementary_filter(accel, gyro, dt, alpha=0.98, pitch_rad=0.0, roll_rad=0.0):
    gx_rad = math.radians(gyro[0])
    gy_rad = math.radians(gyro[1])

    accel_pitch = math.atan2(accel[0], accel[2])
    accel_roll = math.atan2(-accel[1], accel[2])

    gyro_pitch = pitch_rad + gy_rad * dt
    gyro_roll = roll_rad + gx_rad * dt

    pitch_rad = alpha * gyro_pitch + (1.0 - alpha) * accel_pitch
    roll_rad = alpha * gyro_roll + (1.0 - alpha) * accel_roll

    pitch_deg = math.degrees(pitch_rad)
    roll_deg = math.degrees(roll_rad)

    return pitch_deg, roll_deg, pitch_rad, roll_rad


def apply_complementary_filter(df, alpha=0.98):
    timestamps = df['timestamp_us'].to_numpy(dtype=np.float64)
    accel_x = df['accel_x'].to_numpy(dtype=np.float64)
    accel_y = df['accel_y'].to_numpy(dtype=np.float64)
    accel_z = df['accel_z'].to_numpy(dtype=np.float64)
    gyro_x = df['gyro_x'].to_numpy(dtype=np.float64)
    gyro_y = df['gyro_y'].to_numpy(dtype=np.float64)

    n = len(df)
    pitch_deg = np.empty(n, dtype=np.float64)
    roll_deg = np.empty(n, dtype=np.float64)
    pitch_rad = 0.0
    roll_rad = 0.0

    for i in range(n):
        dt = (timestamps[i] - timestamps[0]) / 1e6 if i == 0 else (timestamps[i] - timestamps[i - 1]) / 1e6

        pitch_d, roll_d, pitch_rad, roll_rad = complementary_filter(
            [accel_x[i], accel_y[i], accel_z[i]],
            [gyro_x[i], gyro_y[i], 0.0],
            dt,
            alpha,
            pitch_rad,
            roll_rad,
        )

        pitch_deg[i] = pitch_d
        roll_deg[i] = roll_d

    df = df.copy()
    df['pitch_deg'] = pitch_deg
    df['roll_deg'] = roll_deg
    return df


def compute_tare_offsets(df, settle_samples=500, tare_samples=100,
                         roll_col='roll_deg', pitch_col='pitch_deg'):
    start = settle_samples
    end = settle_samples + tare_samples

    if end > len(df):
        end = len(df)
        start = max(0, end - tare_samples)

    roll_offset = float(np.mean(df[roll_col].iloc[start:end]))
    pitch_offset = float(np.mean(df[pitch_col].iloc[start:end]))

    return roll_offset, pitch_offset, start, end


def apply_taring(df, settle_samples=500, tare_samples=100,
                 roll_col='roll_deg', pitch_col='pitch_deg',
                 out_roll_col='roll_tared', out_pitch_col='pitch_tared'):
    roll_offset, pitch_offset, start, end = compute_tare_offsets(
        df, settle_samples, tare_samples, roll_col, pitch_col
    )

    df = df.copy()
    df[out_roll_col] = df[roll_col] - roll_offset
    df[out_pitch_col] = df[pitch_col] - pitch_offset

    return df, roll_offset, pitch_offset, start, end
