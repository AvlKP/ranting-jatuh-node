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

### Requirement: Spectral acoustic emission processing SHALL be bounded
High-rate acoustic emission ADC sampling and FFT processing SHALL use bounded task execution, persistent buffers, and observable backpressure.

#### Scenario: Spectral AE task hot path
- **WHEN** spectral acoustic emission mode is enabled and one 256-sample window is processed
- **THEN** the acquisition and FFT processing path SHALL avoid heap allocation
- **AND** it SHALL avoid file I/O and network I/O
- **AND** it SHALL use bounded loops over fixed-size buffers only

#### Scenario: Spectral AE buffers are not stack-heavy
- **WHEN** implementation code stores ADC windows, FFT input, FFT output, or gradient history for spectral acoustic emission processing
- **THEN** those buffers SHALL use bounded persistent storage or explicitly measured task stack storage
- **AND** verification SHALL record stack high-water margin for the AE processing task when the task exists

#### Scenario: Spectral AE event backpressure is visible
- **WHEN** spectral acoustic emission detection publishes failures faster than the ESP event loop can accept them
- **THEN** failed event posts SHALL be counted or logged without blocking the spectral processing task indefinitely
- **AND** diagnostics SHALL expose the dropped failure count through existing monitor failure drop reporting

#### Scenario: Spectral AE build records resource impact
- **WHEN** spectral acoustic emission mode is enabled for validation
- **THEN** verification SHALL record `idf.py size` output
- **AND** verification SHALL record boot logs showing critical tasks started
- **AND** verification SHALL include runtime evidence that monitor, logger, and AE processing tasks remain alive during spectral detection

### Requirement: Cross-core diagnostics SHALL be concurrency-safe
Firmware diagnostics exposed across FreeRTOS tasks or ESP32-S3 cores SHALL use atomics, bounded critical sections, or locked snapshot APIs.

#### Scenario: Dashboard reads drop counters while tasks update them
- **WHEN** the dashboard status handler reads monitor or logger drop counters while monitor, logger, or event-loop tasks update those counters
- **THEN** the values SHALL be read through a concurrency-safe API
- **AND** no data race or torn compound snapshot SHALL occur

#### Scenario: Verification reads task state while firmware runs
- **WHEN** startup verification reads monitor state, task handles, or diagnostic counters
- **THEN** the values SHALL be read through a concurrency-safe API
- **AND** the read SHALL NOT block monitor sampling for unbounded time

### Requirement: Firmware test builds SHALL compile before runtime validation
Relevant Unity test builds SHALL compile before a change is considered ready for hardware validation.

#### Scenario: Monitor test build compiles
- **WHEN** `idf.py -B build-test -D TEST_COMPONENTS=monitor build` is run
- **THEN** the build SHALL complete successfully
- **AND** test code SHALL use assertion macros available in the ESP-IDF Unity version in use

### Requirement: Hot-path callbacks SHALL avoid heap-backed type erasure
Transport callbacks used by monitor sample reads SHALL avoid heap-backed type erasure and unbounded dispatch overhead.

#### Scenario: IMU sample loop invokes transport callbacks
- **WHEN** the monitor task reads accelerometer and gyroscope samples from the LSM6DS3 driver
- **THEN** callback dispatch SHALL use fixed-size function pointers, static adapters, or another heap-free mechanism
- **AND** the sample path SHALL NOT depend on `std::function` allocation behavior

### Requirement: Init-time heap use SHALL be isolated from realtime paths
Any unavoidable heap allocation by ESP-IDF services or optional filters SHALL occur only during initialization or non-realtime network/dashboard paths.

#### Scenario: Optional EKF allocation is not in monitor sample path
- **WHEN** the normal monitor sample loop executes
- **THEN** it SHALL NOT allocate or free EKF or ESP-DSP objects dynamically
- **AND** optional heap-backed filters SHALL NOT be instantiated from the per-sample update path

### Requirement: Network runtime verification SHALL prove stack margin
Hardware verification SHALL include stack evidence for the network task during successful and failed publish cycles.

#### Scenario: Network task publishes several files
- **WHEN** `network_task` completes a publish cycle that includes MQTT connect, at least two failure files, and at least two sealed parameter files
- **THEN** no stack overflow or stack canary panic SHALL occur
- **AND** the log SHALL record stack high-water margin for `network_task`

#### Scenario: Backoff after publish error
- **WHEN** a publish or sent-transition error causes network backoff
- **THEN** the task SHALL remain alive
- **AND** stack diagnostics SHALL still be available after the error path

### Requirement: Changed files SHALL pass whitespace checks
Implementation changes SHALL pass repository whitespace validation before a change is considered ready for merge.

#### Scenario: Git whitespace check
- **WHEN** `git diff --check` is run after implementation
- **THEN** it SHALL report no trailing whitespace, conflict markers, or whitespace errors in changed files

### Requirement: Central NVS initialization SHALL be single-owner
Normal application startup SHALL initialize NVS through one shared owner before components access persistent storage.

#### Scenario: App startup initializes NVS once
- **WHEN** `app_main` begins normal firmware startup
- **THEN** NVS SHALL be initialized before monitor calibration, logger node-id, network strategy, WiFi, or MQTT helpers access NVS
- **AND** components SHALL NOT independently erase or reinitialize NVS after startup has succeeded

#### Scenario: Compatibility wrapper uses shared initializer
- **WHEN** an existing component entry point requires NVS for backward compatibility
- **THEN** it SHALL call a shared idempotent initializer or detect initialized state safely
- **AND** it SHALL NOT maintain a divergent module-local initialization flag that can become stale

