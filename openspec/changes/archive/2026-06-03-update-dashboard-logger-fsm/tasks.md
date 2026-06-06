## 1. Logger Updates

- [x] 1.1 Update `ParameterHeader()` in `components/logger/logger_storage.cpp` to add `natural_freq_roll_hz`, `natural_freq_pitch_hz`, and `state`.
- [x] 1.2 Update `FormatParameterCsv()` in `components/logger/logger.cpp` to output a proper CSV string instead of a JSON string, matching the new header format.

## 2. Dashboard Updates

- [x] 2.1 Update the HTML template in `components/dashboard/dashboard.cpp` to display `FREE_DECAY` as a recognized state.
- [x] 2.2 Update the HTML template to display `natural_freq_roll_hz` and `natural_freq_pitch_hz` as separate values.
- [x] 2.3 Update the frontend JavaScript within `components/dashboard/dashboard.cpp` to parse the new fields from the `/api/monitor` endpoint and populate the UI elements correctly.
