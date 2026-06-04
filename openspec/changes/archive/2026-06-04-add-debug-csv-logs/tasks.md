## 1. Configuration

- [x] 1.1 Add a configuration flag in `main/config.hpp` (or relevant config file) to toggle CSV debug logging on or off.

## 2. Monitor Module Updates

- [x] 2.1 Update the `monitor` component to extract or compute tilt data (`tilt_x`, `tilt_y`, `tilt_z`) alongside the acceleration data.
- [x] 2.2 Modify the internal data structure or queue payload to pass this tilt data to the `logger` component.

## 3. Logger Module Updates

- [x] 3.1 Update the `logger` initialization to check the CSV debug logging flag.
- [x] 3.2 If enabled, open a specific CSV debug file (e.g., `/sdcard/accel_tilt_debug.csv`) in `w` mode to clear previous logs and prepare for a new run.
- [x] 3.3 Write the CSV header row: `timestamp_ms,accel_x,accel_y,accel_z,tilt_x,tilt_y,tilt_z` to the debug file.
- [x] 3.4 In the logger's main task, intercept raw acceleration and tilt data, format it as a CSV string, and append it to the debug file.
- [x] 3.5 Add a filter or bypass in the MQTT transmission pipeline to ensure that these specific debug logs are excluded from being sent to the server.
