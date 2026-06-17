## 1. Configuration

- [x] 1.1 Add `CONFIG_MONITOR_FREEFALL_DEBOUNCE_MS` Kconfig entry (`components/monitor/Kconfig`) with default 10000, range 0–3600000, help text
- [x] 1.2 Add `freefall_debounce_ms` field to `MonitorConfig` struct (`components/monitor/include/monitor.hpp`) initialized from `CONFIG_MONITOR_FREEFALL_DEBOUNCE_MS`

## 2. Cooldown State

- [x] 2.1 Add `last_freefall_publish_us_` private member (`std::uint64_t`, default 0) to `Monitor` class in `monitor.hpp`

## 3. Cooldown Gate

- [x] 3.1 In `Monitor::CheckFailureEvents()` (`components/monitor/monitor.cpp`), gate free-fall publish: only call `PublishFailure(FailureEvent::FreeFall)` if `esp_timer_get_time() - last_freefall_publish_us_ >= config_.freefall_debounce_ms * 1000`
- [x] 3.2 Update `last_freefall_publish_us_` with current `esp_timer_get_time()` timestamp after successful free-fall publish

## 4. Unit Tests

- [x] 4.1 Add test case: free-fall fires on first detection when no prior publish
- [x] 4.2 Add test case: free-fall suppressed when elapsed time < debounce duration
- [x] 4.3 Add test case: free-fall fires again after debounce duration elapses
- [x] 4.4 Add test case: acoustic emission failure unaffected by free-fall cooldown
- [x] 4.5 Add test case: config field reflects Kconfig default value

## 5. Build & Verify

- [x] 5.1 Run `idf.py build` to confirm compilation succeeds
- [x] 5.2 Run unit tests with `idf.py build && idf.py flash monitor` on target or verify via CI
