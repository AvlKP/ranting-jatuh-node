## ADDED Requirements

### Requirement: Critical task stacks SHALL be allocated before WiFi subsystem init

The firmware SHALL create `logger_task` and `monitor_task` FreeRTOS tasks before any `esp_wifi_init()`, `esp_wifi_start()`, or `esp_wifi_connect()` call to avoid internal heap fragmentation from WiFi/lwIP/mbedTLS allocations.

#### Scenario: ENT WiFi boot creates tasks successfully
- **WHEN** the firmware boots with `CONFIG_LOGGER_WIFI_ENT_ENABLE` active and enterprise WiFi configured
- **THEN** `logger_task` SHALL be created before `esp_wifi_init()` is called
- **AND** `monitor_task` SHALL be created before `esp_wifi_init()` is called
- **AND** heap diagnostics after task creation SHALL report `largest_block` greater than or equal to the configured task stack sizes

#### Scenario: PSK WiFi boot creates tasks successfully
- **WHEN** the firmware boots with standard PSK WiFi and dashboard enabled
- **THEN** `logger_task` SHALL be created before `esp_wifi_init()` is called
- **AND** `monitor_task` SHALL be created before `esp_wifi_init()` is called
- **AND** boot SHALL continue to the normal all-tasks-started state

#### Scenario: Logger Init backward compatibility
- **WHEN** code calls `logger::Logger::Init()` as before
- **THEN** WiFi initialization SHALL still occur during `Init()` for backward compatibility
- **AND** the split `InitCore()`/`StartWifi()` API SHALL be available for callers that need separate phases

### Requirement: Logger MQTT subsystem SHALL support phased initialization

The `logger::mqtt` namespace SHALL expose a public `InitCore()` function that initializes NVS, event groups, and event handlers without touching WiFi hardware, and a public `StartWifi()` function that performs WiFi driver init and connection.

#### Scenario: InitCore does not allocate WiFi buffers
- **WHEN** `logger::mqtt::InitCore()` is called
- **THEN** NVS SHALL be initialized
- **AND** WiFi and IP event handlers SHALL be registered
- **AND** static event groups SHALL be created
- **AND** `esp_wifi_init()` SHALL NOT be called
- **AND** `esp_wifi_start()` SHALL NOT be called

#### Scenario: StartWifi runs after InitCore
- **WHEN** `logger::mqtt::StartWifi()` is called after `InitCore()`
- **THEN** `esp_wifi_init()` SHALL be called
- **AND** WiFi mode, config, and connection SHALL proceed as before
- **AND** the function SHALL return true on success or false with an error log on failure
