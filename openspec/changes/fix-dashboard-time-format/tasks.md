## 1. Implement Epoch Calculation

- [ ] 1.1 In `components/dashboard/dashboard.cpp` inside `StatusHandler`, retrieve `esp_timer_get_time()` and system `time_t`
- [ ] 1.2 Implement the age-based microsecond epoch calculation for each stream sample inside the serialization loop
- [ ] 1.3 Update the JSON chunked response writer to serialize the calculated epoch timestamp (or uptime fallback) as `ts`

## 2. Compile and Verify

- [ ] 2.1 Build the firmware using `idf.py build`
- [ ] 2.2 Verify that the dashboard client correctly displays real local clock time in Asia/Singapore (GMT+8) or local timezone instead of `07:00:xx AM`
- [ ] 2.3 Confirm that the fallback logic operates cleanly before NTP sync completes (or with network disconnected)
