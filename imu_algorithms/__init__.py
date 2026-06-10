"""Tree branch biomechanical parameter extraction from IMU data.

Provides disturbance detection, natural frequency estimation, damping
ratio, sway amplitude, and tilt angle extraction from LSM6DS3TR-C sensor
data. Designed as reference implementation for ESP32-S3 firmware deployment.

Two analysis planes:
  Plane 1 (Real-time):  Per-sample TKEO + Schmitt trigger state machine.
                         O(1) per sample, fixed state, no heap allocation.
  Plane 2 (Post-hoc):   Full-segment TKEO energy-burst decay onset,
                         signed-axis FFT, peak-hold envelope, bounded OLS
                         log-fit damping. Runs only on disturbance exit.

Usage:
    from imu_algorithms import Pipeline, run_pipeline

    results = run_pipeline("raw_log_7.csv")
"""

from ._extraction import Pipeline, run_pipeline

__all__ = ["Pipeline", "run_pipeline"]
