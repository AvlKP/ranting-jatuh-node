## Context

The `/api/status` endpoint in `components/dashboard/dashboard.cpp` serializes a list of stream samples to JSON. Each sample includes a `ts` field representing `stream_buf[i].timestamp_us`. The value of `timestamp_us` is generated inside `components/monitor/monitor.cpp` using `esp_timer_get_time()`, which measures monotonic uptime in microseconds since boot.
The dashboard frontend page formats this timestamp using `new Date(sample.ts / 1000).toLocaleTimeString()`.
Because `new Date()` expects epoch milliseconds (time since 1970-01-01 00:00:00 UTC), and the dashboard sends microseconds since boot, the formatted time always starts at `07:00:00 AM` (GMT+7) or `08:00:00 AM` (GMT+8) plus the board's uptime.

## Goals / Non-Goals

**Goals:**
- Correctly format the sensor stream table time columns in the dashboard UI using real wall-clock time once NTP time sync succeeds.
- Keep the design backwards-compatible: do not require any changes to the HTML/JS dashboard client code.
- Gracefully fall back to uptime relative to epoch (the current behavior) if the system clock has not yet successfully synchronized.

**Non-Goals:**
- Change the `monitor` component sampling logic or replace `esp_timer_get_time()`. Monotonic time remains correct for high-frequency measurements.
- Introduce additional API calls or state endpoints.

## Decisions

### 1. Calculate absolute epoch timestamps during JSON serialization

**Decision:** Inside `Dashboard::StatusHandler` in `components/dashboard/dashboard.cpp`, dynamically calculate the absolute epoch timestamp (in microseconds) for each sample:
```cpp
const std::uint64_t current_uptime_us = esp_timer_get_time();
std::time_t current_time = 0;
std::time(&current_time);

std::uint64_t sample_ts_us = 0;
if (current_time >= 1672531200) {
    const std::uint64_t current_time_us = static_cast<std::uint64_t>(current_time) * 1000000ULL;
    if (current_uptime_us >= stream_buf[i].timestamp_us) {
        const std::uint64_t age_us = current_uptime_us - stream_buf[i].timestamp_us;
        sample_ts_us = current_time_us - age_us;
    } else {
        sample_ts_us = current_time_us;
    }
} else {
    sample_ts_us = stream_buf[i].timestamp_us;
}
```

**Rationale:** Highly localized change in `dashboard.cpp`. No frontend modification required. If the system time is valid, it accurately reconstructs each sample's real-world timestamp in microseconds by subtracting its relative age from the current system epoch time. If system time is unsynced (epoch ~0), it falls back to raw uptime, preserving baseline behavior.

**Alternative considered:** Modifying the JS client to read a `system_boot_time` property and compute local dates — rejected because it requires HTML/JS parsing, increases payload sizes, and is more complex than direct server-side translation.

## Risks / Trade-offs

- **Small computation overhead in StatusHandler loop** (simple subtraction and multiplication per sample) → Completely negligible.
- **Clock skew / adjustments**: If the system time is corrected/updated via NTP while the stream is active, the reconstructed epoch time will jump cleanly, reflecting the true real-world wall clock without gaps or system lockups.
