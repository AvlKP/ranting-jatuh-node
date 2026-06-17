## Context

Field mode currently selects `network_on_demand.cpp` when `CONFIG_DASHBOARD_ENABLE` is unset. That strategy starts WiFi for an eligible publish cycle, waits for IP, lets the network task publish via MQTT, then calls `esp_wifi_disconnect()` and `esp_wifi_stop()` to save power.

Runtime log `build/log/idf_py_stdout_output_2956` shows the first on-demand cycle succeeds: WPA2-Enterprise connects, DHCP completes, MQTT connects, and the parameter file publishes. The strategy then intentionally disconnects and receives `reason=8 (assoc_leave)`. The second cycle reaches `assoc -> run`, but the application times out at 20 seconds; ESP-IDF later reports `reason=204 (handshake_timeout)` from the 30-second Enterprise/4-way handshake path. Later retries appear to enter timeout without a full fresh `Start wifi connect` sequence, which suggests failed attempts can leave started or inconsistent WiFi state behind.

Runtime log `build/log/idf_py_stdout_output_3752` shows a second failure mode after lifecycle fixes were applied: the AP does not respond to the 802.11 authentication frame within ESP-IDF's internal 1-second AUTH timer, producing `reason=2 (auth_expire)`. ESP-IDF then blacklists the BSSID. A single-attempt strategy gives up immediately and falls through to network-task backoff (~120 seconds), making the node appear unresponsive for minutes even though a retry would likely succeed.

## Goals / Non-Goals

**Goals:**

- Preserve on-demand power behavior for field mode.
- Make repeated on-demand WiFi cycles reliable after success, timeout, and disconnect events.
- Wait long enough for WPA2-Enterprise handshake failure to surface as a real disconnect reason.
- Ensure failed connect attempts always leave the radio and event state ready for the next attempt.
- Improve diagnostics so logs distinguish intentional release, handshake timeout, application timeout, and stale-state defects.

**Non-Goals:**

- Do not switch field mode to persistent WiFi.
- Do not add hybrid hold-open policy in this change.
- Do not change MQTT payloads, topics, SD outbox format, or logger event flow.
- Do not introduce deep-sleep or full power-policy redesign.

## Decisions

### Use explicit on-demand lifecycle state

Represent on-demand WiFi lifecycle with small internal state, for example `Stopped`, `Starting`, `Connecting`, `Connected`, and `Releasing`. Event handlers update durable state only when events match the expected lifecycle. Intentional release sets `Releasing` before `esp_wifi_disconnect()`, so the resulting `assoc_leave` or `auth_leave` event does not count as connect failure.

Alternative considered: keep only event-group bits. That already loses meaning because the same bits are used for waiting and for state, and disconnect events from expected release look identical to real failures.

### Separate wait signals from durable connection state

Use event bits only as one-shot wake signals for the current connect attempt. Keep durable state such as `s_started`, `s_connected`, `s_connecting`, and `s_intentional_disconnect` separately. `IsConnected()` reads durable state and is not affected by `xEventGroupWaitBits(..., clearOnExit=true)`.

Alternative considered: keep event bits but avoid `clearOnExit`. That reduces one bug but still couples state, wakeups, and delayed disconnect events.

### Align connect timeout with ESP-IDF Enterprise timing

Raise on-demand connect timeout above ESP-IDF's 30-second 4-way handshake timer; use 45 seconds unless measurements show a better project default. The wait still ends on `GOT_IP` or a disconnect event, but does not report generic timeout before the lower-layer reason can arrive.

Alternative considered: keep 20 seconds for battery. That saves time only on failures while hiding the real `reason=204` and can leave WiFi active until the driver finishes its own timeout.

### Cleanup inside failed `EnsureConnected()`

If on-demand connection fails by disconnect event, application timeout, API error, or missing credentials, `EnsureConnected()` should stop the WiFi cycle before returning false. Cleanup should tolerate partially started state and ignore expected "not started" or "not connected" errors. After cleanup, the next `EnsureConnected()` must perform a full fresh start/connect sequence.

Alternative considered: require `network_task` to call `ReleaseConnection()` after `EnsureConnected()` returns false. That spreads lifecycle ownership across modules and makes failure cleanup easier to miss.

### Preserve power behavior after successful publish

`ReleaseConnection()` remains the field-mode boundary that disconnects and stops WiFi after MQTT publish. It should wait or otherwise serialize enough of the stop sequence so the next on-demand connect cannot race stale disconnect or stop events.

Alternative considered: switch to persistent WiFi for WPA2-Enterprise. That is likely more reliable, but it changes the power model and should be a separate policy change.

### Retry transient failures within `EnsureConnected()`

Wrap the WiFi start/wait/check cycle in a retry loop (up to 3 attempts). When a disconnect reason is classified as transient (`auth_expire`, `assoc_expire`, `handshake_timeout`, `4way_timeout`, `beacon_timeout`, `no_ap_found`), call `CleanupWifi()` — which stops WiFi and clears ESP-IDF's internal BSSID blacklist — wait 1 second for AP recovery, then perform a fresh `esp_wifi_start()`. WiFi configuration and EAP setup happen once before the loop; only the start/wait/cleanup cycle repeats.

Non-transient reasons (`auth_fail`, `mic_failure`, `assoc_fail`) fail immediately since retrying wrong credentials or a rejected association is pointless.

Alternative considered: rely on `network_task` exponential backoff for all retries. That introduces a 60–120+ second gap between attempts, making the node appear dead for minutes after a single transient auth failure that would succeed on immediate retry.

## Risks / Trade-offs

- Longer failed-connect timeout increases worst-case radio-on time -> keep exponential backoff and stop WiFi after failure.
- Ignoring intentional `assoc_leave` could hide a release bug -> log it as expected release at debug/info level, not warning/error.
- Extra state can drift from ESP-IDF state -> reset state from `WIFI_EVENT_STA_STOP`, `WIFI_EVENT_STA_DISCONNECTED`, and API cleanup paths.
- Enterprise AP selection may still choose a bad BSSID -> lifecycle fix will expose clean failures; BSSID pinning or roaming policy can be a later change if needed.
- Waiting for cleanup events can deadlock if event delivery is broken -> use bounded waits and tolerant cleanup.
- Retry loop increases worst-case radio-on time per `EnsureConnected()` call (up to 3 × 45s + delays) -> bounded by attempt count; non-transient reasons still fail immediately.

## Migration Plan

1. Implement lifecycle state and separated wait/state bits in `network_on_demand.cpp`.
2. Increase the on-demand connect timeout to cover ESP-IDF Enterprise handshake reporting.
3. Add cleanup-on-failure inside `EnsureConnected()`.
4. Classify intentional release disconnects separately from real connect failures.
5. Add retry loop for transient disconnect reasons with full stop/start between attempts.
6. Extract reason classification into testable header; add focused tests.
7. Verify on hardware with the observed WPA2-Enterprise flow: first publish succeeds, release is classified expected, second connect either succeeds or reports `reason=204` without poisoning later retries. Verify transient `auth_expire` is retried successfully.

Rollback: revert to previous on-demand lifecycle if field testing shows regression. Existing SD outbox data and MQTT behavior are unchanged.
