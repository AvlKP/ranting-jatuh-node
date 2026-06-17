## Context

`Monitor::CheckFailureEvents()` reads the LSM6DS3 `WAKE_UP_SRC` register every sample cycle (52 Hz). When the free-fall bit is set, it calls `PublishFailure(FailureEvent::FreeFall)` unconditionally. The hardware re-latches the bit on every ODR cycle while acceleration remains below threshold, so a single physical event produces a rapid burst of failure publishes -- flooding the ESP event loop, logger queue, SD outbox, and MQTT notifications.

The existing `MONITOR_FF_DUR` Kconfig parameter controls only hardware-level debounce (how many ODR cycles the LSM6DS3 waits before latching the free-fall flag). There is no software-level cooldown.

Existing patterns in the codebase:
- AE spectral detector uses `spectral_latch_duration_ms` and `spectral_min_publish_interval_ms` for similar publish-rate-limiting.
- DISTURBED→IDLE transition uses `dsp_quiet_debounce` (sample-count debounce).

## Goals / Non-Goals

**Goals:**
- Suppress duplicate free-fall failure publishes within a configurable cooldown window (default 10 seconds).
- Minimize memory overhead (one timestamp per failure type, currently just free-fall).

**Non-Goals:**
- Debounce acoustic emission failures (already handled by AE spectral latch/min-interval).
- Add cooldown for other failure types beyond free-fall.
- Implement a generalized failure-cooldown framework (keep it simple, single-purpose).

## Decisions

**1. Timestamp-gate in CheckFailureEvents, not in PublishFailure**

Rationale: Gating at the check site keeps `PublishFailure` as a thin wrapper. The caller decides whether to publish; the publisher just publishes. This matches how `CheckAeFailureEvents` gates AE failures internally.

Alternative considered: Gate inside `PublishFailure`. Rejected because it would require `PublishFailure` to know about cooldown state, mixing concerns. It would also require a map lookup per call, even for unblocked events.

**2. Single timestamp per failure type, not a generic map**

Rationale: Only free-fall needs debounce today. A `std::array<std::uint64_t, 2>` indexed by `FailureEvent` enum is 16 bytes, trivial. No dynamic allocation, no map overhead.

Alternative considered: General-purpose `FreeRTOSTimer` or `esp_timer` callback. Overkill for a simple timestamp check. Adds timer handle, callback, ISR-safe concerns.

**3. Configurable via Kconfig + MonitorConfig, default 10,000 ms**

Rationale: Default matches user requirement. Kconfig allows tuning without recompile of monitor component only. `MonitorConfig` allows runtime override in tests.

**4. Cooldown starts at publish time, not at detection time**

Rationale: The timestamp is recorded when `PublishFailure` is actually called, not when the hardware bit is read. This ensures the cooldown is based on actual publishes, not transient hardware flags that may be suppressed.

## Risks / Trade-offs

- **Rapid successive physical free-falls within cooldown window → silently dropped**: Legitimate second free-fall within 10 seconds would be suppressed. Mitigation: 10-second default is conservative for branch monitoring. Tunable via Kconfig for different deployment scenarios.
- **Wake-up from sleep resets timestamp to 0 → first free-fall after wake always fires**: This is correct behavior. Timestamp 0 means "never published," so cooldown check always passes on first event.
- **Timestamp overflow**: `esp_timer_get_time()` returns `int64_t` microseconds, wraps only after ~292,000 years. Not a practical concern.
