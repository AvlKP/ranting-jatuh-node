## 1. Network Strategy Module

- [x] 1.1 Create `components/logger/include/network_strategy.hpp` with shared interface: `Init()`, `EnsureConnected()`, `ReleaseConnection()`, `IsConnected()` in `logger::network` namespace.
- [x] 1.2 Create `components/logger/network_on_demand.cpp` implementing on-demand strategy: full WiFi connect/disconnect cycle per publish, with NVS init, event handler registration, and event group management (extract from current `ConnectWifi()`/`InitWifiCore()`).
- [x] 1.3 Create `components/logger/network_persistent.cpp` implementing persistent strategy: WiFi stays up, auto-reconnect on disconnect, `EnsureConnected()` checks event group bit, `ReleaseConnection()` is no-op.
- [x] 1.4 Update `components/logger/CMakeLists.txt` to conditionally link `network_persistent.cpp` (when `CONFIG_DASHBOARD_ENABLE`) or `network_on_demand.cpp` (otherwise).
- [x] 1.5 Remove all `#if CONFIG_DASHBOARD_ENABLE` WiFi guards from `logger_mqtt.cpp` — replace with calls to `network::EnsureConnected()` / `network::ReleaseConnection()`.

## 2. SD Upload Queue

- [x] 2.1 Create `components/logger/include/outbox.hpp` with interface: `Init(mount_point)`, `AppendParameter(json_line)`, `AppendFailure(json_line)`, `RotateParameterFile()`, `GetPendingFiles(out_list)`, `MarkSent(filename)`, `PruneSent(max_keep)`.
- [x] 2.2 Create `components/logger/outbox.cpp` implementing the SD outbox: create `outbox/pending/` and `outbox/sent/` directories on init, append JSONL lines to current parameter file, create individual failure files, scan pending directory, move files to sent, prune old sent files.
- [x] 2.3 Add file rotation logic: start new `params_<epoch>.jsonl` when current file age exceeds `CONFIG_LOGGER_WIFI_PERIOD_HOURS`.
- [x] 2.4 Add boot recovery: on `Init()`, scan `pending/` for existing files from prior boot and include them in the pending list.

## 3. Network Task

- [x] 3.1 Create `components/logger/include/network_task.hpp` with interface: `Init(mount_point)`, `Start()`, `EnqueueNotify()` (lightweight notification that new data is available).
- [x] 3.2 Create `components/logger/network_task.cpp` implementing the FreeRTOS task (core 0, priority 3): loop on task notification, scan SD outbox, connect via `network::EnsureConnected()`, publish pending files line-by-line, move to sent, call `network::ReleaseConnection()`.
- [x] 3.3 Implement `BackoffState` struct with exponential backoff: 60s initial, double on fail, 1hr max, jitter via `esp_random()`, reset on success. Network task checks `ShouldSkip()` before each connection attempt.
- [x] 3.4 Implement failure file priority: sort pending file list with `failure_*` files before `params_*` files.
- [x] 3.5 Implement MQTT client lifecycle with proper cleanup: `EnsureMqttClient()` destroys handle + nulls pointer on any mid-init failure (subsumes `fix-mqtt-client-state-on-error`).
- [x] 3.6 Implement `sent/` pruning: after successful publish batch, delete oldest files in `sent/` if count exceeds retention limit (default 10).
- [x] 3.7 Move NTP sync into network task: call `SyncTime()` after WiFi connect, before MQTT publish.

## 4. Logger Task Refactor

- [x] 4.1 Reorder failure event handling in `Logger::TaskLoop()`: call `storage::AppendFailure()` first, then `outbox::AppendFailure()`. Remove direct `mqtt::PublishFailure()` call.
- [x] 4.2 Replace `mqtt::PublishParameters()` call with `outbox::AppendParameter()` + `network_task::EnqueueNotify()`. Remove `g_pending_params` buffer and `g_publish_batch` array.
- [x] 4.3 Format both CSV (for SD log) and JSON (for outbox) for each parameter event. Write CSV to SD log via `storage::AppendParameter()`, write JSON to outbox via `outbox::AppendParameter()`.
- [x] 4.4 Remove all `#if !CONFIG_DASHBOARD_ENABLE` / `esp_wifi_disconnect()` / `esp_wifi_stop()` calls from logger task code.
- [x] 4.5 Remove `PublishParameters()`, `PublishFailure()`, `PublishRaw()`, `PublishLines()`, `PublishRawInternal()`, `ConnectWifi()`, `SyncTime()`, `EnsureMqttClient()` from `logger_mqtt.cpp` (functionality moved to network task + network strategy).

## 5. Main Integration

- [x] 5.1 Update `main.cpp` boot sequence: init outbox after SD mount, init network strategy, start network task before logger task.
- [x] 5.2 Update `Logger::Init()` to accept outbox reference/pointer and remove `mqtt::InitCore()` call.
- [x] 5.3 Update `Logger::VerifyMqttPublish()` to use network task or remove (verify via network task startup publish).
- [x] 5.4 Move `mqtt::StartWifi()` and `mqtt::SyncTimeOnce()` calls in `main.cpp` to network task init sequence.

## 6. Cleanup

- [x] 6.1 Remove or archive change `fix-mqtt-client-state-on-error` — its fix is subsumed by task 3.5.
- [x] 6.2 Update `logger_internal.hpp` to remove `mqtt::PublishParameters`, `mqtt::PublishFailure`, `mqtt::PublishRaw`, `mqtt::SyncTimeOnce` declarations. Add `network_task` and `outbox` includes.
- [x] 6.3 Remove `kPublishPeriodUs`, `next_publish_us_`, `PendingParamsBuffer`, `g_pending_params`, `g_publish_batch` from logger code.

## 7. Verification

- [x] 7.1 Build with `CONFIG_DASHBOARD_ENABLE=y` — verify persistent strategy compiles and links.
- [x] 7.2 Build with `CONFIG_DASHBOARD_ENABLE=n` — verify on-demand strategy compiles and links.
- [ ] 7.3 Flash and test normal publish flow: parameters appear on MQTT broker, files cycle through `pending/` → `sent/`.
- [ ] 7.4 Test WiFi-down scenario: disconnect AP, verify logger task continues SD writes, verify outbox files accumulate in `pending/`, verify no crash or task starvation.
- [ ] 7.5 Test reconnect scenario: restore AP after outage, verify backoff resets, pending files published, moved to `sent/`.
- [ ] 7.6 Test failure event during outage: trigger free-fall, verify SD log written immediately, verify failure JSONL file created in `pending/`.
- [ ] 7.7 Test reboot recovery: reboot with files in `pending/`, verify they are published on reconnect.
