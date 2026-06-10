"""Raw IMU CSV loading utilities.

Reads raw_log_*.csv files produced by the ESP32 raw logger mode
(APP_BUILD_RAW_LOGGER). Columns: timestamp_us, accel_x, accel_y,
accel_z, gyro_x, gyro_y, gyro_z, temp_c.
"""

import numpy as np
import pandas as pd


def load_imu(path: str) -> pd.DataFrame:
    """Load a raw IMU CSV file into a DataFrame with named columns.

    Args:
        path: Path to raw IMU CSV file.

    Returns:
        DataFrame with columns: timestamp_us, accel_x, accel_y,
        accel_z, gyro_x, gyro_y, gyro_z, temp_c.
    """
    cols = [
        "timestamp_us", "accel_x", "accel_y", "accel_z",
        "gyro_x", "gyro_y", "gyro_z", "temp_c",
    ]
    df = pd.read_csv(path, header=None, names=cols)
    return df
