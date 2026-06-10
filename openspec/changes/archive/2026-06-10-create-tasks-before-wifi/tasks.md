## 1. Split Logger MQTT API

- [x] 1.1 Extract `InitCore()` from `InitWifiCore()` — a public function that initializes NVS, creates static event groups, and registers WiFi/IP event handlers without calling `esp_wifi_init()`.
- [x] 1.2 Add public `StartWifi()` function that calls `InitWifiCore()` (idempotent), configures SSID/password/ENT, and starts WiFi connection. Reuses existing `StartWifiPersistent()` logic.
- [x] 1.3 Update `Init()` to internally call `InitCore()` + `StartWifi()` for backward compatibility with existing callers.
- [x] 1.4 Add `InitCore()` and `StartWifi()` declarations to `logger_internal.hpp`.

## 2. Reorder Boot Sequence

- [x] 2.1 In `app_main()`, call `logger::mqtt::InitCore()` instead of relying on `logger.Init()` for WiFi setup.
- [x] 2.2 Move `logger.Start()` and `monitor.Start()` immediately after `logger.Init()`, before any WiFi or NTP calls.
- [x] 2.3 Add a heap checkpoint (`LogHeapDiagnostics("post_tasks_pre_wifi")`) after task creation to verify `largest_block` is sufficient.
- [x] 2.4 Call `logger::mqtt::StartWifi()` after tasks are created, before `SyncTimeOnce()`.
- [x] 2.5 Verify `SyncTimeOnce()` and dashboard ordering remain correct after the reorder.
- [x] 2.6 Re-evaluate WiFi buffer reductions: with tasks allocated before WiFi, revert `sdkconfig.defaults` dynamic buffer reductions (16→32) if ENT auth needs larger contiguous pool. Keep static RX at 6 and LWIP recvmbox at 16. Rationale: WiFi stack needs ~3000+ bytes contiguous for beacon/probe/4-way-handshake; prior reduction was compensating for task stacks competing with the same pool.

## 3. Verification

- [x] 3.1 Run `idf.py build` and confirm no compilation errors.
- [x] 3.2 Run `idf.py size` and record DIRAM impact.
- [x] 3.3 Flash/monitor with ENT WiFi config and confirm `logger_task`, `monitor_task` start successfully with `largest_block` >= task stack sizes.
- [x] 3.4 Flash/monitor with PSK WiFi config and confirm all tasks plus dashboard start successfully.
- [x] 3.5 Verify backward compatibility: confirm `Logger::Init()` still works for code that calls the combined init (e.g., test paths).
