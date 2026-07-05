## MODIFIED Requirements

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

## ADDED Requirements

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
