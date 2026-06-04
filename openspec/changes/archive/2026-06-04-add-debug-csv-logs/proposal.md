## Why

The system currently only logs raw acceleration data at the DEBUG level, which makes manual offline analysis difficult. By logging tilt and acceleration data into CSV files during runtime, we can easily plot and observe dynamic behavior without polluting the regular logs sent to the server.

## What Changes

- Add tilt data logging at the DEBUG level.
- Introduce a configuration option to enable CSV format debug logging specifically for raw acceleration and tilt data.
- Overwrite (rewrite) the CSV debug log files at the beginning of each run to prevent file size unbounded growth and focus on the latest run.
- Prevent these debug log files from being transmitted to the server.

## Capabilities

### New Capabilities
- `debug-csv-logs`: Configurable CSV file logging for raw acceleration and tilt data that writes locally on the device, overwrites on startup, and is excluded from server transmission.

### Modified Capabilities

## Impact

- **Logger Module**: Changes to how logs are formatted when CSV debugging is enabled. File writing logic needs to overwrite old debug files on startup. Add filtering to ensure debug logs are not sent to the server.
- **Monitor/Sensor Module**: Emitting tilt data at the DEBUG level alongside the acceleration data.
- **Configuration Module**: A new flag added to enable this mode.
