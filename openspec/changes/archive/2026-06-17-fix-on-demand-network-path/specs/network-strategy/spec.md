## ADDED Requirements

### Requirement: On-demand connection attempts SHALL be self-contained
The on-demand WiFi strategy SHALL leave WiFi driver, event, and connection state clean after every failed `EnsureConnected()` attempt.

#### Scenario: Cleanup after connect timeout
- **WHEN** `EnsureConnected()` starts an on-demand WiFi cycle
- **AND** the attempt times out before IP acquisition
- **THEN** the strategy SHALL stop the WiFi cycle before returning `false`
- **AND** the next `EnsureConnected()` call SHALL perform a fresh WiFi start and connect sequence

#### Scenario: Cleanup after disconnect failure
- **WHEN** `EnsureConnected()` starts an on-demand WiFi cycle
- **AND** ESP-IDF reports `WIFI_EVENT_STA_DISCONNECTED` before IP acquisition
- **THEN** the strategy SHALL record the disconnect reason
- **AND** it SHALL stop the WiFi cycle before returning `false`
- **AND** the next `EnsureConnected()` call SHALL NOT be affected by stale failure bits from the previous attempt

#### Scenario: Cleanup after partial API failure
- **WHEN** an on-demand connect attempt fails after WiFi has been partially started or configured
- **THEN** the strategy SHALL run bounded cleanup before returning `false`
- **AND** cleanup SHALL tolerate already-stopped or already-disconnected WiFi states

### Requirement: On-demand Enterprise connect wait SHALL cover lower-layer handshake timing
The on-demand WiFi strategy SHALL wait long enough for ESP-IDF's WPA/WPA2 handshake failure path to report a disconnect reason before declaring an application-level timeout.

#### Scenario: Handshake timeout reason is not hidden
- **WHEN** an on-demand connection reaches association or WPA/WPA2 handshake state
- **AND** IP acquisition does not complete
- **THEN** the on-demand wait SHALL NOT expire before ESP-IDF's 30-second 4-way handshake timer can report failure
- **AND** if ESP-IDF reports `WIFI_REASON_HANDSHAKE_TIMEOUT`, diagnostics SHALL preserve that reason

#### Scenario: Early disconnect ends wait
- **WHEN** ESP-IDF reports a disconnect event before the configured on-demand timeout expires
- **THEN** `EnsureConnected()` SHALL return `false` after cleanup without waiting for the full timeout period

### Requirement: Intentional on-demand release SHALL NOT be classified as connect failure
The on-demand WiFi strategy SHALL distinguish expected disconnect events caused by `ReleaseConnection()` from unexpected disconnect events during connection attempts.

#### Scenario: Successful publish release
- **WHEN** `ReleaseConnection()` disconnects and stops WiFi after a successful publish cycle
- **AND** ESP-IDF reports `WIFI_REASON_ASSOC_LEAVE` or `WIFI_REASON_AUTH_LEAVE`
- **THEN** the strategy SHALL classify the event as expected release cleanup
- **AND** it SHALL NOT set failure state for the next on-demand connection attempt

#### Scenario: Unexpected disconnect during connect
- **WHEN** an on-demand connection attempt is in progress
- **AND** ESP-IDF reports a disconnect reason other than expected release cleanup
- **THEN** the strategy SHALL classify the event as a connection failure
- **AND** it SHALL expose the reason in diagnostics

### Requirement: On-demand connection state SHALL be independent from wait signals
The on-demand WiFi strategy SHALL keep durable connection state separate from one-shot event signals used to wake waiting tasks.

#### Scenario: Successful connect leaves connected state readable
- **WHEN** `EnsureConnected()` receives `IP_EVENT_STA_GOT_IP` and returns `true`
- **THEN** `IsConnected()` SHALL return `true` until a real disconnect or release occurs
- **AND** repeated calls to `IsConnected()` SHALL NOT clear the connected state

#### Scenario: Release clears connected state
- **WHEN** `ReleaseConnection()` completes or begins on-demand WiFi shutdown
- **THEN** `IsConnected()` SHALL return `false`
- **AND** the next `EnsureConnected()` SHALL clear one-shot wait signals before starting a new attempt

### Requirement: On-demand cleanup SHALL be bounded
The on-demand WiFi strategy SHALL avoid unbounded waits while cleaning up failed or released WiFi cycles.

#### Scenario: Stop event does not arrive
- **WHEN** the strategy requests WiFi shutdown during cleanup
- **AND** the expected stop or disconnect event does not arrive
- **THEN** cleanup SHALL finish after a bounded timeout
- **AND** the strategy SHALL reset internal lifecycle state consistently before the next attempt

### Requirement: On-demand connect SHALL retry transient failures
The on-demand WiFi strategy SHALL retry connection attempts when the disconnect reason indicates a transient failure, up to a bounded maximum.

#### Scenario: Auth expire retried successfully
- **WHEN** `EnsureConnected()` starts an on-demand WiFi cycle
- **AND** ESP-IDF reports `WIFI_REASON_AUTH_EXPIRE` before IP acquisition
- **AND** the maximum retry count has not been reached
- **THEN** the strategy SHALL stop WiFi (clearing the BSSID blacklist), wait briefly, and perform a fresh start/connect cycle
- **AND** if IP is acquired on the retry, `EnsureConnected()` SHALL return `true`

#### Scenario: Transient failure retries exhausted
- **WHEN** `EnsureConnected()` has retried a transient failure up to the maximum attempt count
- **AND** the connection still fails
- **THEN** the strategy SHALL stop WiFi, log the final reason, and return `false`
- **AND** the caller's backoff timer SHALL govern the next `EnsureConnected()` call

#### Scenario: Non-transient failure is not retried
- **WHEN** `EnsureConnected()` receives a disconnect with a non-transient reason such as `WIFI_REASON_AUTH_FAIL`
- **THEN** the strategy SHALL NOT retry
- **AND** it SHALL stop WiFi and return `false` immediately
