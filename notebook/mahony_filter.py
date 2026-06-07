import math
import numpy as np


def mahony_filter(accel, gyro, dt,
                  Kp=0.5,
                  Ki=0.1,
                  k_gain=50.0,
                  q=(1.0, 0.0, 0.0, 0.0),
                  bias=(0.0, 0.0, 0.0)):
    ax, ay, az = accel[0], accel[1], accel[2]
    gx, gy, gz = math.radians(gyro[0]), math.radians(gyro[1]), math.radians(gyro[2])

    accel_mag = math.sqrt(ax * ax + ay * ay + az * az)
    error = abs(accel_mag - 1.0)
    weight = 1.0 / (1.0 + k_gain * error)

    if accel_mag > 1e-8:
        ax /= accel_mag
        ay /= accel_mag
        az /= accel_mag

    q0, q1, q2, q3 = q

    vx = 2.0 * (q1 * q3 - q0 * q2)
    vy = 2.0 * (q0 * q1 + q2 * q3)
    vz = q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3

    ex = ay * vz - az * vy
    ey = az * vx - ax * vz
    ez = ax * vy - ay * vx

    Kp_eff = Kp * weight
    Ki_eff = Ki * weight

    b0, b1, b2 = bias
    b0 += Ki_eff * ex * dt
    b1 += Ki_eff * ey * dt
    b2 += Ki_eff * ez * dt
    bias = (b0, b1, b2)

    gx_corr = gx + Kp_eff * ex + b0
    gy_corr = gy + Kp_eff * ey + b1
    gz_corr = gz + Kp_eff * ez + b2

    dq0 = 0.5 * (-q1 * gx_corr - q2 * gy_corr - q3 * gz_corr)
    dq1 = 0.5 * ( q0 * gx_corr + q2 * gz_corr - q3 * gy_corr)
    dq2 = 0.5 * ( q0 * gy_corr - q1 * gz_corr + q3 * gx_corr)
    dq3 = 0.5 * ( q0 * gz_corr + q1 * gy_corr - q2 * gx_corr)

    q0 += dq0 * dt
    q1 += dq1 * dt
    q2 += dq2 * dt
    q3 += dq3 * dt

    q_norm = math.sqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3)
    if q_norm > 1e-8:
        q0 /= q_norm
        q1 /= q_norm
        q2 /= q_norm
        q3 /= q_norm
    q = (q0, q1, q2, q3)

    pitch_rad = math.asin(2.0 * (q0 * q2 - q3 * q1))
    roll_rad = math.atan2(2.0 * (q0 * q1 + q2 * q3),
                          1.0 - 2.0 * (q1 * q1 + q2 * q2))

    pitch_deg = math.degrees(pitch_rad)
    roll_deg = math.degrees(roll_rad)

    return pitch_deg, roll_deg, q, bias, weight


def apply_mahony_filter(df, Kp=0.5, Ki=0.1, k_gain=50.0):
    timestamps = df['timestamp_us'].to_numpy(dtype=np.float64)
    accel_x = df['accel_x'].to_numpy(dtype=np.float64)
    accel_y = df['accel_y'].to_numpy(dtype=np.float64)
    accel_z = df['accel_z'].to_numpy(dtype=np.float64)
    gyro_x = df['gyro_x'].to_numpy(dtype=np.float64)
    gyro_y = df['gyro_y'].to_numpy(dtype=np.float64)
    gyro_z = df['gyro_z'].to_numpy(dtype=np.float64)

    n = len(df)
    pitch_deg = np.empty(n, dtype=np.float64)
    roll_deg = np.empty(n, dtype=np.float64)
    weight_hist = np.empty(n, dtype=np.float64)
    q = (1.0, 0.0, 0.0, 0.0)
    bias = (0.0, 0.0, 0.0)

    for i in range(n):
        dt = ((timestamps[i] - timestamps[0]) / 1e6 if i == 0
              else (timestamps[i] - timestamps[i - 1]) / 1e6)

        pitch_d, roll_d, q, bias, weight = mahony_filter(
            [accel_x[i], accel_y[i], accel_z[i]],
            [gyro_x[i], gyro_y[i], gyro_z[i]],
            dt, Kp, Ki, k_gain, q, bias,
        )

        pitch_deg[i] = pitch_d
        roll_deg[i] = roll_d
        weight_hist[i] = weight

    df = df.copy()
    df['pitch_mahony'] = pitch_deg
    df['roll_mahony'] = roll_deg
    df['mahony_weight'] = weight_hist
    return df
