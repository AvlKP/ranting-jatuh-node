## ADDED Requirements

### Requirement: Failure-triggered publish cycles SHALL respect parameter cadence
An urgent failure publish cycle SHALL upload failure files before the parameter cadence expires, but SHALL NOT upload parameter files early.

#### Scenario: Failure and sealed parameters pending before cadence
- **WHEN** one or more failure files are pending
- **AND** one or more sealed parameter files are pending
- **AND** the parameter upload cadence is not due
- **THEN** the network task SHALL publish eligible failure files only
- **AND** it SHALL leave sealed parameter files pending for a later cadence-due cycle

#### Scenario: Failure and sealed parameters pending after cadence
- **WHEN** one or more failure files are pending
- **AND** one or more sealed parameter files are pending
- **AND** the parameter upload cadence is due
- **THEN** the network task SHALL publish failure files first
- **AND** it MAY publish sealed parameter files in the same successful network cycle

### Requirement: Network publish path SHALL remain stack-safe under multi-file batches
The network task SHALL survive worst-case configured pending-file batches without stack canary panic or stack overflow.

#### Scenario: Multi-file publish batch
- **WHEN** the pending outbox contains failure files and the maximum configured number of sealed parameter files returned by the scanner
- **AND** WiFi, SNTP, MQTT5 connect, line-by-line publish, sent moves, pruning, and disconnect all execute
- **THEN** `network_task` SHALL remain alive without stack overflow
- **AND** verification SHALL record network task stack high-water margin

#### Scenario: Large local buffers audited
- **WHEN** network publish code needs filename arrays, path buffers, MQTT line buffers, or publish property structs
- **THEN** recurring task-path storage SHALL use bounded static task-owned storage, smaller chunked buffers, or a stack size with measured margin
- **AND** large automatic buffers SHALL be justified by recorded high-water margin

### Requirement: Runtime publish diagnostics SHALL classify real and benign errors
Network verification logs SHALL distinguish fatal publish errors from benign ESP-IDF debug probes.

#### Scenario: SDMMC IO-card probe fails during SD-card init
- **WHEN** ESP-IDF logs `sdmmc_req` or `sdmmc_io` debug messages indicating an IO-card probe returned `0x107`
- **AND** SD card initialization continues and mounts successfully
- **THEN** verification SHALL classify the messages as benign debug probes
- **AND** they SHALL NOT fail outbox runtime verification

#### Scenario: Outbox sent rename reports destination exists
- **WHEN** outbox sent transition encounters a destination-exists condition after successful MQTT publish
- **THEN** diagnostics SHALL identify it as an outbox collision defect unless collision-safe handling succeeds
- **AND** verification SHALL fail if the file remains in pending due to the collision
