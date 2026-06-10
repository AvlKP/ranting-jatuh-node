"""IMU bias calibration.

Calibration constants derived from the first 9 seconds of raw_log_7.csv
(static baseline period) by computing per-axis means.
Subtracting these biases drops the baseline |gyro| from ~2.875 dps
to ~0.36 dps, exposing true motion.

On the ESP32-S3 target, these constants are stored in flash (ROM).
Subtraction is one cycle per axis in the real-time path.
"""

import numpy as np
from typing import Tuple

# Gyro biases (dps) -- mean of first 9s of raw_log_7.csv
BIAS_GX = 1.096412
BIAS_GY = -2.593744
BIAS_GZ = 0.414028

# Accel biases (g) -- mean of first 9s of raw_log_7.csv
BIAS_AX = 0.014925
BIAS_AY = -0.010015
BIAS_AZ = 0.010312


def calibrate(
    gx_raw: float,
    gy_raw: float,
    gz_raw: float,
    ax_raw: float,
    ay_raw: float,
    az_raw: float,
) -> Tuple[float, float, float, float, float, float]:
    """Subtract per-axis bias constants from raw IMU readings.

    Args:
        gx_raw, gy_raw, gz_raw: Raw gyroscope readings in dps.
        ax_raw, ay_raw, az_raw: Raw accelerometer readings in g.

    Returns:
        Tuple of (gx_cal, gy_cal, gz_cal, ax_cal, ay_cal, az_cal).
    """
    return (
        gx_raw - BIAS_GX,
        gy_raw - BIAS_GY,
        gz_raw - BIAS_GZ,
        ax_raw - BIAS_AX,
        ay_raw - BIAS_AY,
        az_raw - BIAS_AZ,
    )


def calibrated_gmag(
    gx_raw: float,
    gy_raw: float,
    gz_raw: float,
) -> float:
    """Calibrate gyro axes then compute magnitude.

    Args:
        gx_raw, gy_raw, gz_raw: Raw gyroscope readings in dps.

    Returns:
        Calibrated gyroscope magnitude sqrt(gx^2 + gy^2 + gz^2) in dps.
    """
    gx, gy, gz, _, _, _ = calibrate(gx_raw, gy_raw, gz_raw, 0.0, 0.0, 0.0)
    return float(np.sqrt(gx * gx + gy * gy + gz * gz))
