## 1. Expose SyncTime API

- [x] 1.1 Move `SyncTime()` from anonymous namespace to `logger::mqtt` namespace in `logger_mqtt.cpp`, rename to `SyncTimeOnce()`
- [x] 1.2 Add `bool SyncTimeOnce() noexcept` declaration to `components/logger/include/logger_internal.hpp`

## 2. Implement Startup Sync Function

- [x] 2.1 Create `bool SyncTimeOnce() noexcept` that calls `ConnectWifi()`, `SyncTime()`, and disconnects WiFi in non-dashboard mode (matching publish path cleanup pattern)
- [x] 2.2 Keep internal `SyncTime()` as-is for use by `PublishLines()` / `PublishRawInternal()` (no disconnect — WiFi already managed by caller)

## 3. Integrate into Boot Sequence

- [x] 3.1 In `main.cpp` `app_main()`, call `logger::mqtt::SyncTimeOnce()` after `logger.Init()` and before `logger.Start()` / `monitor.Start()`
- [x] 3.2 Log warning on sync failure, do not abort boot

## 4. Verify

- [ ] 4.1 Build and flash, confirm NTP sync log appears before monitor/logger task logs
- [ ] 4.2 Verify SD card log timestamps are valid from first entry
- [ ] 4.3 Test with WiFi unavailable — confirm boot continues with warning
