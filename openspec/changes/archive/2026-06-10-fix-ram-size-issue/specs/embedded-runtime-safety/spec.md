## ADDED Requirements

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
