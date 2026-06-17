## 1. On-Demand State Model

- [x] 1.1 Add explicit on-demand lifecycle state in `components/logger/network_on_demand.cpp` for stopped, starting, connecting, connected, releasing, and stopping phases.
- [x] 1.2 Split one-shot wait signals from durable state so `IsConnected()` no longer depends on event bits consumed by `xEventGroupWaitBits()`.
- [x] 1.3 Update WiFi and IP event handlers to set durable state only for events valid in the current lifecycle phase.
- [x] 1.4 Classify disconnect reasons during release (`WIFI_REASON_ASSOC_LEAVE`, `WIFI_REASON_AUTH_LEAVE`) as expected cleanup instead of connection failure.

## 2. Connect And Cleanup Flow

- [x] 2.1 Increase the on-demand connect timeout to cover ESP-IDF's 30-second WPA/WPA2 handshake timer, with a margin for event delivery.
- [x] 2.2 Clear one-shot wait signals and previous failure diagnostics before each new on-demand connect attempt.
- [x] 2.3 Add a bounded cleanup helper that disconnects/stops WiFi when a connect attempt fails, tolerating already-stopped or already-disconnected states.
- [x] 2.4 Call the cleanup helper on every `EnsureConnected()` failure path after WiFi may have been started or configured.
- [x] 2.5 Make `ReleaseConnection()` set release state before disconnecting, clear durable connected state, stop WiFi, and finish with bounded cleanup semantics.
- [x] 2.6 Ensure the next `EnsureConnected()` after any timeout, handshake failure, or release performs a full fresh start/connect sequence.
- [x] 2.7 Add `IsTransientReason()` classifier for retryable disconnect codes (`auth_expire`, `assoc_expire`, `handshake_timeout`, `4way_timeout`, `beacon_timeout`, `no_ap_found`).
- [x] 2.8 Add retry loop in `EnsureConnected()` (up to 3 attempts) with full WiFi stop/start between retries to clear BSSID blacklist, and 1-second delay between attempts.
- [x] 2.9 Fail immediately on non-transient reasons (`auth_fail`, `mic_failure`, etc.) without retrying.

## 3. Diagnostics

- [x] 3.1 Log expected release disconnects separately from unexpected connect failures.
- [x] 3.2 Preserve and report the ESP-IDF disconnect reason when it arrives before or during failed-attempt cleanup.
- [x] 3.3 Make application-level timeout logs distinguish "no disconnect reason observed" from explicit reasons such as `handshake_timeout`.

## 4. Verification

- [x] 4.1 Add focused unit-test coverage or test seams for pure state/reason classification logic (`IsReleaseReason`, `IsTransientReason`, `WifiReasonString`) where practical.
- [x] 4.2 Build the monitor unit-test target with `idf.py -B build-test -D TEST_COMPONENTS=monitor build`.
- [x] 4.3 Build the firmware with `idf.py build`.
- [ ] 4.4 Run hardware verification on WPA2-Enterprise: first publish succeeds, release logs as expected cleanup, second on-demand cycle either connects or reports `reason=204 (handshake_timeout)` without poisoning later retries.
- [ ] 4.5 Verify field-mode power behavior remains on-demand: WiFi starts only for eligible publish cycles and stops after release or failed connect cleanup.
