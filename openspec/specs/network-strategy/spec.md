# network-strategy Specification

## Purpose
TBD - created by archiving change robust-network-failure-handling. Update Purpose after archive.
## Requirements
### Requirement: Compile-time WiFi strategy selection
The system SHALL select between persistent and on-demand WiFi connection strategies at compile time based on the `CONFIG_LOGGER_NETWORK_MODE` Kconfig choice (`on-demand` | `persistent`). `CONFIG_DASHBOARD_ENABLE=y` SHALL imply `CONFIG_LOGGER_NETWORK_MODE=persistent` but SHALL NOT select the strategy source file directly.

#### Scenario: Persistent mode selected by config choice
- **WHEN** the project is built with `CONFIG_LOGGER_NETWORK_MODE=persistent`
- **THEN** the system SHALL link `network_persistent.cpp` and use the persistent network strategy with bounded WiFi auto-reconnect

#### Scenario: On-demand mode selected by config choice
- **WHEN** the project is built with `CONFIG_LOGGER_NETWORK_MODE=on-demand`
- **THEN** the system SHALL link `network_on_demand.cpp` and use the on-demand network strategy with per-publish connect/disconnect

#### Scenario: Dashboard implies persistent
- **WHEN** the project is built with `CONFIG_DASHBOARD_ENABLE=y` and `CONFIG_LOGGER_NETWORK_MODE` is not explicitly set
- **THEN** the build SHALL default `CONFIG_LOGGER_NETWORK_MODE` to `persistent`
- **AND** the persistent strategy file SHALL be linked

#### Scenario: Persistent without dashboard
- **WHEN** the project is built with `CONFIG_DASHBOARD_ENABLE=n` and `CONFIG_LOGGER_NETWORK_MODE=persistent`
- **THEN** the system SHALL use the persistent network strategy without building or starting the HTTP dashboard

### Requirement: Persistent strategy maintains WiFi connection
In persistent mode, the system SHALL keep WiFi connected and automatically reconnect on disconnection using a bounded exponential-backoff policy. The disconnect event handler SHALL NOT call `esp_wifi_connect()` synchronously in a tight loop; reconnect SHALL be delayed by the current backoff interval.

#### Scenario: Auto-reconnect on disconnect with backoff
- **WHEN** WiFi disconnects in persistent mode mid-session
- **THEN** the strategy SHALL schedule a reconnect attempt after a backoff delay
- **AND** the backoff delay SHALL grow exponentially with jitter up to a bounded maximum

#### Scenario: Reconnect interval is capped
- **WHEN** repeated WiFi disconnect events occur in persistent mode
- **THEN** the strategy SHALL cap the reconnect backoff interval at a bounded maximum
- **AND** it SHALL continue retrying at that maximum interval without giving up

#### Scenario: Successful connect resets backoff
- **WHEN** a reconnect attempt succeeds and IP is acquired
- **THEN** the strategy SHALL reset the backoff interval to its initial value

#### Scenario: EnsureConnected returns quickly
- **WHEN** `EnsureConnected()` is called in persistent mode and WiFi is already connected
- **THEN** it SHALL return within 1 ms by checking the event group bit

#### Scenario: EnsureConnected honors backoff when down
- **WHEN** `EnsureConnected()` is called in persistent mode and WiFi is not connected
- **AND** the backoff delay has not elapsed
- **THEN** it SHALL return `false` without forcing an immediate connect
- **AND** it SHALL NOT reset the WiFi backoff timer

### Requirement: On-demand strategy connects per publish cycle
In on-demand mode, the system SHALL connect WiFi only when publishing and disconnect after completion.

#### Scenario: Full connect cycle on publish
- **WHEN** `EnsureConnected()` is called in on-demand mode
- **THEN** it SHALL perform WiFi start, connect, and wait for IP address

#### Scenario: Release disconnects WiFi
- **WHEN** `ReleaseConnection()` is called in on-demand mode after a publish
- **THEN** it SHALL call `esp_wifi_disconnect()` and `esp_wifi_stop()` to power down the radio

#### Scenario: Release is no-op in persistent mode
- **WHEN** `ReleaseConnection()` is called in persistent mode
- **THEN** it SHALL do nothing (WiFi remains connected)

### Requirement: Shared network interface
Both strategies SHALL expose the same function interface: `Init()`, `EnsureConnected()`, `ReleaseConnection()`, `IsConnected()`.

#### Scenario: Network task uses strategy without compile-time guards
- **WHEN** the network task calls `network::EnsureConnected()`
- **THEN** the correct strategy implementation SHALL be invoked based on the build configuration without `#if` guards in the calling code

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

### Requirement: Persistent mode SHALL NOT enable power management
Persistent mode SHALL keep the ESP-IDF Power Management framework disabled (`CONFIG_ESP_PM_ENABLE` unset) and SHALL explicitly set the WiFi station interface power-save to a no-PS mode so demo latency stays predictable. The system SHALL NOT enter light-sleep or dynamic frequency scaling.

#### Scenario: PM stays disabled in persistent mode
- **WHEN** the project is built with `CONFIG_LOGGER_NETWORK_MODE=persistent`
- **THEN** `CONFIG_ESP_PM_ENABLE` SHALL remain unset in sdkconfig
- **AND** the system SHALL NOT call `esp_pm_configure` to enable light-sleep or DFS

#### Scenario: WiFi power-save explicitly off
- **WHEN** the persistent strategy initializes WiFi before connecting
- **THEN** it SHALL call `esp_wifi_set_ps(WIFI_PS_NONE)` (or the IDF no-PS equivalent)
- **AND** the CPU frequency SHALL remain at the configured 160 MHz during persistent operation

### Requirement: Persistent WiFi backoff state SHALL be static and allocation-free
The persistent WiFi backoff state SHALL live in static storage and SHALL NOT allocate from the heap. The WiFi event handler SHALL remain safe to invoke from the event loop task without taking locks that could deadlock against `EnsureConnected()`.

#### Scenario: No heap allocation in reconnect path
- **WHEN** the disconnect event handler schedules a reconnect in persistent mode
- **THEN** it SHALL NOT call `malloc`, `new`, or any allocating ESP-IDF API
- **AND** the backoff state SHALL reside in static (file-scope) storage

#### Scenario: Event handler does not deadlock with EnsureConnected
- **WHEN** `EnsureConnected()` is waiting on the connected event bit
- **AND** a disconnect event fires concurrently
- **THEN** the event handler SHALL NOT take a mutex held by `EnsureConnected()`
- **AND** it SHALL update backoff state with at most atomic/event-group primitives

