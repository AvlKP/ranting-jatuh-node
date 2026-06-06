## Why

The `monitor-fsm-fft-redesign` change introduced new monitor outputs (`natural_freq_roll_hz`, `natural_freq_pitch_hz`, and `state`) and a 3-state FSM. While `monitor` and `mqtt` JSON payload changes were implemented, the CSV logger storage (`logger_storage.cpp`) and the debugging dashboard web interface (`dashboard.cpp`) were not completely updated to reflect these new fields. This leaves the SD card data and debugging UI out of sync with the monitor's new capabilities.

## What Changes

- Update `logger_storage.cpp` parameter CSV header to include `natural_freq_roll_hz`, `natural_freq_pitch_hz`, and `state`.
- Update `logger.cpp`'s `FormatParameterCsv` to write the new fields into the CSV line using CSV format, aligning with the header (it currently seems to format JSON incorrectly for the CSV logging path or is overloading it). Wait, if it writes JSON to a CSV file, I'll need to make sure the CSV actually formats as CSV, not JSON Lines.
- Update `dashboard.cpp` HTML/JS to display the new `FREE_DECAY` state and separate `natural_freq_roll_hz` and `natural_freq_pitch_hz` instead of a single `f_n` value.

## Capabilities

### New Capabilities
None

### Modified Capabilities
- `logger-fsm-adaptation`: Update the CSV file logging format to include the new fields, as the previous change only updated the JSON MQTT payload.
- `dashboard-fsm-adaptation`: Update the dashboard UI to render the new `FREE_DECAY` state and separate roll/pitch frequencies.

## Impact

- `components/logger/logger.cpp`: CSV formatting function changes.
- `components/logger/logger_storage.cpp`: CSV header changes.
- `components/dashboard/dashboard.cpp`: HTTP handler HTML string updates.
