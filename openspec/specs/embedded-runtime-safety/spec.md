# embedded-runtime-safety Specification

## Purpose
Ensure firmware tasks and hot paths have verified stack margin, bounded execution, and observable backpressure to prevent silent runtime failures.
## Requirements
### Requirement: Firmware SHALL maintain measured stack margin

Each firmware task and ESP service task exercised by normal operation SHALL have a measured stack high-water margin after worst-case available workflows are executed. The audit SHALL cover at least `app_main`, `monitor_task`, `logger_task`, dashboard HTTP server task, ESP event task, ESP timer task, and WiFi/MQTT tasks that can be identified at runtime.

#### Scenario: Worst-case monitor workflow has stack margin
- **WHEN** the node samples IMU data, enters `DISTURBED`, exits `DISTURBED`, runs post-hoc modal analysis, publishes monitor events, and resumes `IDLE`
- **THEN** `monitor_task` SHALL remain alive without stack canary panic or compiler stack check panic
- **AND** measured stack high-water margin SHALL be recorded in verification output

#### Scenario: Dashboard workflow has stack margin
- **WHEN** `/api/status` is requested repeatedly while monitor data, FFT data, MQTT logs, and SD file entries are present
- **THEN** the dashboard HTTP server task SHALL remain alive without stack canary panic or compiler stack check panic
- **AND** measured stack high-water margin SHALL be recorded when task handle access is available

#### Scenario: Logger workflow has stack margin
- **WHEN** monitor parameter events, failure events, SD appends, and MQTT publishes are exercised
- **THEN** `logger_task` SHALL remain alive without stack canary panic or compiler stack check panic
- **AND** measured stack high-water margin SHALL be recorded in verification output

### Requirement: Large recurring buffers SHALL NOT use automatic storage in constrained task paths

Firmware SHALL avoid large automatic buffers in recurring task paths, HTTP handlers, event handlers, and monitor compute paths unless the task stack is explicitly sized and measured to retain acceptable margin.

#### Scenario: Static audit finds large local buffers
- **WHEN** implementation code declares automatic arrays, `std::array`, or large structs in monitor, logger, dashboard, event-handler, or ISR-adjacent paths
- **THEN** each allocation SHALL be classified as acceptable with measured stack margin or replaced by bounded persistent storage, chunked streaming, or smaller local buffers

#### Scenario: Kconfig increases buffer size
- **WHEN** a Kconfig value changes storage minutes, IMU rate, FFT size, short buffer size, logger queue depth, or dashboard query buffer length
- **THEN** compile-time checks SHALL reject configurations that exceed documented RAM or stack limits

### Requirement: Hot paths SHALL be deterministic and bounded

Sampling, ISR, event-handler, and logger enqueue paths SHALL avoid unbounded blocking, unbounded loops over external data, dynamic allocation, and file/network I/O.

#### Scenario: Monitor sample update path
- **WHEN** `monitor_task` executes one normal sample update
- **THEN** the path SHALL avoid heap allocation and file/network I/O
- **AND** mutex critical sections SHALL remain bounded to shared state access

#### Scenario: GPIO ISR path
- **WHEN** the acoustic emission GPIO interrupt fires
- **THEN** the ISR SHALL only perform ISR-safe bounded work
- **AND** event processing SHALL be deferred to task context

#### Scenario: ESP event handler path
- **WHEN** monitor result or failure events are delivered to logger or dashboard handlers
- **THEN** handlers SHALL copy bounded payloads and defer slow work
- **AND** handlers SHALL NOT perform SD writes, MQTT publishes, DSP, or blocking waits

### Requirement: Buffer and serialization operations SHALL be bounds-checked

All JSON, CSV, path, topic, and MQTT-log formatting SHALL check output length and avoid writing past fixed buffers. Truncated output SHALL fail safely or be explicitly marked as truncated.

#### Scenario: Long SD filename in dashboard status
- **WHEN** dashboard status lists a regular file with a long filename
- **THEN** generated JSON SHALL remain valid or skip/escape the file entry safely
- **AND** no fixed buffer overflow SHALL occur

#### Scenario: Long MQTT log line in dashboard status
- **WHEN** dashboard status serializes MQTT log lines containing quotes, backslashes, or maximum-length text
- **THEN** generated JSON SHALL escape the content within fixed bounds
- **AND** no fixed buffer overflow SHALL occur

#### Scenario: CSV and JSON logger formatting
- **WHEN** logger formats parameter or failure records
- **THEN** formatter return values SHALL be checked
- **AND** records that do not fit SHALL not be written or published as valid data

### Requirement: Queue and event backpressure SHALL be observable

Firmware SHALL detect and expose dropped monitor/logger events caused by full queues or failed posts so runtime overload is visible during validation.

#### Scenario: Logger queue full
- **WHEN** monitor events arrive faster than `logger_task` can process them
- **THEN** the logger SHALL drop events using zero-wait behavior
- **AND** dropped event counters SHALL be incremented
- **AND** verification or diagnostics SHALL expose the drop count

#### Scenario: ESP event post fails
- **WHEN** `esp_event_post` fails while publishing monitor result or failure events
- **THEN** the failure SHALL be counted or logged without blocking the monitor task indefinitely

### Requirement: Firmware SHALL maintain boot heap margin for critical tasks

The normal monitoring firmware build SHALL start all critical runtime tasks without exhausting internal heap. Critical runtime tasks include `logger_task`, `monitor_task`, ESP event/timer service tasks, WiFi/lwIP tasks, and the dashboard HTTP server task when `CONFIG_DASHBOARD_ENABLE` is active.

#### Scenario: Normal dashboard boot starts monitor and logger
- **WHEN** the firmware boots with WiFi, SD logging, monitor, logger, and dashboard enabled
- **THEN** `logger_task` SHALL be created successfully
- **AND** `monitor_task` SHALL be created successfully
- **AND** boot SHALL continue to the normal all-tasks-started state

#### Scenario: Boot keeps allocatable internal heap margin
- **WHEN** all normal boot services and critical runtime tasks have started
- **THEN** diagnostics SHALL report free internal heap and largest allocatable internal block
- **AND** the largest allocatable internal block SHALL remain larger than the configured stack size of the largest remaining optional task or documented validation threshold

### Requirement: Task creation failures SHALL include memory diagnostics

Every application-owned FreeRTOS task creation failure in normal startup SHALL log enough information to diagnose RAM pressure.

#### Scenario: Monitor task creation fails
- **WHEN** creating `monitor_task` returns a value other than `pdPASS`
- **THEN** firmware SHALL log the task name, requested stack size, free internal heap, and largest free internal allocation block
- **AND** firmware SHALL return startup failure without reporting that all tasks started

#### Scenario: Logger task creation fails
- **WHEN** creating `logger_task` returns a value other than `pdPASS`
- **THEN** firmware SHALL log the task name, requested stack size, free internal heap, and largest free internal allocation block
- **AND** firmware SHALL return startup failure without reporting that all tasks started

### Requirement: RAM reductions SHALL preserve measured stack safety

Changes that reduce FreeRTOS task stack sizes, WiFi buffer counts, dashboard HTTP server stack size, or monitor/logger static buffers SHALL be validated with runtime stack and heap diagnostics.

#### Scenario: Stack or buffer budget changes
- **WHEN** implementation changes stack sizes, WiFi/lwIP buffer counts, dashboard HTTP server stack size, or monitor/logger RAM budgets
- **THEN** verification SHALL record `idf.py size` output
- **AND** verification SHALL record boot logs showing critical tasks started
- **AND** verification SHALL record stack high-water margins for application-owned tasks when task handles are available

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

