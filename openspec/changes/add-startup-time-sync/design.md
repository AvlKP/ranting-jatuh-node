## Context

Currently `SyncTime()` lives in the anonymous namespace of `logger_mqtt.cpp` and is only called within `PublishLines()` / `PublishRawInternal()`. WiFi init happens in `logger::mqtt::Init()` at boot, but no NTP sync follows. The system clock stays at epoch 0 until the first MQTT publish cycle (minutes after boot). All SD log entries and timestamps before that point are invalid.

The `SyncTime()` function does: init SNTP in poll mode → wait up to 15s → stop SNTP. It's stateless and safe to call multiple times.

## Goals / Non-Goals

**Goals:**
- System clock valid before monitor and logger tasks start collecting data.
- Reuse existing `SyncTime()` and `ConnectWifi()` logic — no duplication.
- Non-blocking to overall boot if sync fails (log warning, continue with unsynchronized clock).

**Non-Goals:**
- Persistent SNTP daemon (keep current init/sync/stop pattern).
- RTC hardware backup.
- Changing the periodic sync behavior in publish paths.

## Decisions

### 1. Expose `SyncTime()` as `logger::mqtt::SyncTimeOnce()`

**Decision:** Move `SyncTime()` from anonymous namespace to `logger::mqtt` namespace. Expose via `logger_internal.hpp` as `bool SyncTimeOnce() noexcept`.

**Rationale:** Minimal change. Function is already stateless. Renaming avoids confusion with the internal-only version. The `ConnectWifi()` already handles the dashboard persistent-WiFi path via `CONFIG_DASHBOARD_ENABLE`, so it works correctly in both modes.

**Alternative considered:** Creating a standalone `time_sync` component — rejected as over-engineering for a single function call.

### 2. Call sync from `app_main()` after `logger::mqtt::Init()` and before tasks start

**Decision:** In `main.cpp`, after `logger.Init()` (which calls `logger::mqtt::Init()` internally), call `logger::mqtt::SyncTimeOnce()`. If it fails, log a warning but don't abort boot.

**Rationale:** WiFi must be initialized before SNTP can work. `logger::mqtt::Init()` does WiFi init. The sync must happen before `logger.Start()` and `monitor.Start()` to ensure timestamps are valid from the first sample. Non-fatal because periodic sync will eventually correct the clock.

**Alternative considered:** Syncing inside `Logger::Init()` — rejected because it couples time sync to logger initialization, and the logger shouldn't own system-wide clock responsibility.

### 3. WiFi connection for sync in non-dashboard mode

**Decision:** In non-dashboard mode, `SyncTimeOnce()` will call `ConnectWifi()`, sync, then disconnect WiFi to save power. In dashboard mode, WiFi is already persistent so it just syncs.

**Rationale:** Matches existing power management pattern in `PublishParameters()` / `PublishFailure()` which disconnect WiFi after publishing in non-dashboard mode.

## Risks / Trade-offs

- **Boot delay up to ~35s** (20s WiFi timeout + 15s NTP timeout) in worst case when network unavailable → Mitigation: sync is non-fatal; system continues with epoch-0 clock. Log warning clearly.
- **WiFi connect/disconnect churn** in non-dashboard mode (connect at boot for sync, disconnect, reconnect later for publish) → Acceptable tradeoff. Power savings from not keeping WiFi on outweigh the reconnection cost. Same pattern already used by publish paths.
