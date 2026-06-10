import numpy as np
import pandas as pd


def load_imu(path: str) -> pd.DataFrame:
    cols = [
        "timestamp_us", "accel_x", "accel_y", "accel_z",
        "gyro_x", "gyro_y", "gyro_z", "temp_c",
    ]
    df = pd.read_csv(path, header=None, names=cols)
    df["timestamp_s"] = (df["timestamp_us"] - df["timestamp_us"].iloc[0]) / 1e6
    return df
