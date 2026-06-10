## ADDED Requirements

### Requirement: Compile-time WiFi strategy selection
The system SHALL select between persistent and on-demand WiFi connection strategies at compile time based on `CONFIG_DASHBOARD_ENABLE`.

#### Scenario: Dashboard mode uses persistent strategy
- **WHEN** the project is built with `CONFIG_DASHBOARD_ENABLE=y`
- **THEN** the system SHALL use the persistent network strategy with WiFi auto-reconnect

#### Scenario: Field mode uses on-demand strategy
- **WHEN** the project is built with `CONFIG_DASHBOARD_ENABLE=n` (or unset)
- **THEN** the system SHALL use the on-demand network strategy with per-publish connect/disconnect

### Requirement: Persistent strategy maintains WiFi connection
In persistent mode, the system SHALL keep WiFi connected and automatically reconnect on disconnection.

#### Scenario: Auto-reconnect on disconnect
- **WHEN** WiFi disconnects in persistent mode
- **THEN** the system SHALL call `esp_wifi_connect()` automatically via the disconnect event handler

#### Scenario: EnsureConnected returns quickly
- **WHEN** `EnsureConnected()` is called in persistent mode and WiFi is already connected
- **THEN** it SHALL return within 1 ms by checking the event group bit

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
