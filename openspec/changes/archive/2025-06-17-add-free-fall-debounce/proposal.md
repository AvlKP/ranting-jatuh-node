## Why

Free-fall failure alerts fire on every sample cycle while the LSM6DS3 hardware free-fall flag is latched. A single physical free-fall event spans multiple ODR cycles, producing a burst of duplicate `FailureEvent::FreeFall` publishes -- flooding the ESP event loop, logger queue, outbox, and network notifications. This wastes storage, bandwidth, and power. A software cooldown prevents rapid re-triggering of the same failure type.

## What Changes

- Add a per-failure-type cooldown mechanism to `Monitor`. After `PublishFailure(FreeFall)` fires, suppress subsequent free-fall failures for a configurable duration (default 10 seconds).
- Expose the cooldown duration via Kconfig (`CONFIG_MONITOR_FREEFALL_DEBOUNCE_MS`) and `MonitorConfig`.
- Track last-publish timestamp per `FailureEvent` type in the monitor.
- Gate `CheckFailureEvents()` to skip free-fall alert when within cooldown window.

## Capabilities

### New Capabilities
- `free-fall-debounce`: Suppress repeated free-fall failure alerts within a configurable cooldown window after each publish.

### Modified Capabilities
<!-- None: no existing spec-level requirements change -->

## Impact

- Affected code: `components/monitor/monitor.cpp` (CheckFailureEvents), `components/monitor/monitor_publisher.cpp` (PublishFailure), `components/monitor/include/monitor.hpp` (MonitorConfig, Monitor private state), `components/monitor/Kconfig`
- No breaking changes to API, event format, or failure data structure
- No new dependencies
- Power/storage/bandwidth reduction from eliminating duplicate failure events
