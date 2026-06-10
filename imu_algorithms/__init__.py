"""IMU algorithms for tree-branch biomechanical parameter extraction.

Provides disturbance detection, natural frequency estimation, damping
ratio, sway amplitude, and tilt angle extraction from LSM6DS3TR-C sensor
data. Designed for eventual ESP32-S3 deployment.

Usage:
    from imu_algorithms import Pipeline, run_pipeline

    results = run_pipeline("raw_log_7.csv")
"""

from ._extraction import Pipeline, run_pipeline

__all__ = ["Pipeline", "run_pipeline"]
