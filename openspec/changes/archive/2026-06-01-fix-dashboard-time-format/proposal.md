## Why

The dashboard real-time stream table always shows the system time starting at `07:00:xx AM` (or `08:00:xx AM`) regardless of whether NTP time synchronization has completed successfully. This happens because the API `/api/status` serializes the raw monotonic uptime `timestamp_us` as the `ts` field in JSON, and the dashboard browser frontend formats it using `new Date(sample.ts / 1000)` which treats uptime milliseconds as epoch milliseconds.

## What Changes

- Modify `/api/status` JSON serialization in `dashboard.cpp` to convert the raw monotonic sample `timestamp_us` to an absolute epoch timestamp (in microseconds) using the current synchronized system time and system uptime.
- If the system time is not yet synchronized, fall back to serializing raw uptime as before.

## Capabilities

### New Capabilities
- `dashboard-time-format-fix`: Real-time sensor stream samples on the dashboard correctly display the synchronized absolute wall-clock time instead of uptime relative to epoch.

### Modified Capabilities
_None — no requirement changes to other specs._

## Impact

- **`components/dashboard/dashboard.cpp`**: Update `StatusHandler` to calculate and serialize epoch microseconds instead of raw monotonic uptime in `stream_samples` JSON payload.
