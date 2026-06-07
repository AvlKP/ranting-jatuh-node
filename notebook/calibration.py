ACCEL_X_BIAS = 0.014925
ACCEL_Y_BIAS = -0.010015
ACCEL_Z_BIAS = 0.010312

GYRO_X_BIAS = 1.096412
GYRO_Y_BIAS = -2.593744
GYRO_Z_BIAS = 0.414028


def apply_calibration(df):
    df = df.copy()
    df['accel_x'] = df['accel_x'] - ACCEL_X_BIAS
    df['accel_y'] = df['accel_y'] - ACCEL_Y_BIAS
    df['accel_z'] = df['accel_z'] - ACCEL_Z_BIAS
    df['gyro_x'] = df['gyro_x'] - GYRO_X_BIAS
    df['gyro_y'] = df['gyro_y'] - GYRO_Y_BIAS
    df['gyro_z'] = df['gyro_z'] - GYRO_Z_BIAS
    return df
