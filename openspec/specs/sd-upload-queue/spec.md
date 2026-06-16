# sd-upload-queue Specification

## Purpose
TBD - created by archiving change robust-network-failure-handling. Update Purpose after archive.
## Requirements
### Requirement: Unsent MQTT payloads persist on SD
The system SHALL write all MQTT payloads to SD card files in the `outbox/pending/` directory before any MQTT publish attempt.

#### Scenario: Parameter payload persisted to SD
- **WHEN** the logger task formats a parameter JSON payload
- **THEN** it SHALL append the payload as a line to the current `pending/params_<epoch>.jsonl` file

#### Scenario: Failure payload persisted to SD
- **WHEN** the logger task formats a failure payload
- **THEN** it SHALL write the payload to an individual `pending/failure_<epoch>.jsonl` file

#### Scenario: New parameter file per publish period
- **WHEN** the current parameter file's epoch exceeds `CONFIG_LOGGER_WIFI_PERIOD_HOURS` since creation
- **THEN** the logger task SHALL create a new `pending/params_<epoch>.jsonl` file for subsequent payloads

### Requirement: Sent files are pruned
The system SHALL move successfully-published files to `outbox/sent/` and prune old sent files to prevent unbounded SD usage.

#### Scenario: Pruning old sent files
- **WHEN** the `sent/` directory contains more files than the configured retention limit (default: 10)
- **THEN** the oldest files in `sent/` SHALL be deleted until the count equals the retention limit

### Requirement: Outbox survives reboot
The system SHALL scan the `outbox/pending/` directory on boot and queue any existing files for upload.

#### Scenario: Pending files uploaded after reboot
- **WHEN** the network task starts and finds files in `outbox/pending/`
- **THEN** it SHALL publish those files before any newly-created files

### Requirement: SD outbox does not block logger task
All SD outbox write operations by the logger task SHALL complete within bounded time and SHALL NOT involve network I/O.

#### Scenario: Logger writes during network outage
- **WHEN** WiFi is unreachable
- **THEN** the logger task SHALL continue writing payloads to `outbox/pending/` without any delay or error related to network state

### Requirement: Active parameter file SHALL NOT be uploaded
The SD outbox SHALL distinguish the active parameter append file from sealed pending files. The network task SHALL NOT publish, rename, move, or delete the active parameter append file.

#### Scenario: Network scans while logger has active parameter file
- **WHEN** the logger task has an active `params_*` file open for future appends
- **AND** the network task scans `outbox/pending/`
- **THEN** the active parameter file SHALL be excluded from the returned publish list

#### Scenario: Logger appends while network publishes sealed files
- **WHEN** the network task is publishing sealed pending files
- **AND** the logger task appends a new parameter payload
- **THEN** the append SHALL target the current active parameter file
- **AND** the network task SHALL NOT move that active file to `sent/`

### Requirement: Parameter files SHALL be sealed before parameter upload
The system SHALL make a parameter file immutable before it becomes eligible for upload.

#### Scenario: Publish period expires
- **WHEN** the configured parameter publish period has elapsed
- **AND** an active parameter file contains parameter payloads
- **THEN** the outbox SHALL seal the active parameter file
- **AND** subsequent parameter appends SHALL use a new active file for the current or next period
- **AND** the sealed file SHALL become eligible for network upload

#### Scenario: Reboot with pending parameter files
- **WHEN** the node boots and finds existing `outbox/pending/params_*` files from a previous boot
- **THEN** those files SHALL be treated as sealed
- **AND** they SHALL be eligible for upload before newly-created parameter files

### Requirement: Outbox shared state SHALL be serialized
Outbox state shared by logger and network tasks SHALL be protected by a bounded lock or equivalent synchronization.

#### Scenario: Logger and network access outbox concurrently
- **WHEN** the logger task appends a payload while the network task scans, seals, marks sent, or prunes files
- **THEN** active filename state, pending directory state, and reusable path buffers SHALL be accessed under synchronization
- **AND** no shared path buffer or active filename state SHALL be modified concurrently

### Requirement: Failure files SHALL remain immutable after creation
Failure payload files SHALL be complete and eligible for urgent upload after their write succeeds.

#### Scenario: Failure payload persisted
- **WHEN** the logger task writes a failure JSON payload to a `failure_*` file
- **THEN** the file SHALL be immutable after the write succeeds
- **AND** the file SHALL be eligible for upload on the next allowed network cycle

### Requirement: Parameter file lifecycle SHALL use immutable sealed files
The SD outbox SHALL use distinct lifecycle states for parameter files: active files accept appends, sealed files are immutable and eligible for upload, and sent files are retained after successful upload.

#### Scenario: Logger appends while network seals
- **WHEN** the logger task appends a parameter payload while the network task is sealing or publishing parameter data
- **THEN** the append SHALL target an active file that is not returned by upload scans
- **AND** the network task SHALL NOT publish, move, delete, or rename the file receiving that append

#### Scenario: Cadence seal makes immutable upload file
- **WHEN** the parameter upload cadence is due
- **AND** the active parameter file contains at least one payload
- **THEN** the outbox SHALL seal it into an immutable pending file
- **AND** subsequent appends SHALL use a different active file identity

### Requirement: Outbox filenames SHALL remain unique when wall-clock time is invalid
Parameter and failure filenames SHALL avoid collisions even before SNTP or RTC time is valid.

#### Scenario: Parameter payload before time sync
- **WHEN** a parameter payload is written before wall-clock time reaches the minimum valid epoch
- **THEN** the pending filename SHALL include a boot-unique or monotonic sequence component
- **AND** it SHALL NOT use a reusable `params_0.jsonl` filename as the only identity

#### Scenario: Failure payloads in same second
- **WHEN** multiple failure payloads are created within the same wall-clock second
- **THEN** each failure file SHALL have a unique pending filename
- **AND** no failure payload SHALL overwrite or append to an unrelated failure file unless that behavior is explicitly designed and tested

### Requirement: Marking sent SHALL be collision-safe
The outbox SHALL handle an existing destination file in `outbox/sent/` without leaving a successfully published pending file stuck in `outbox/pending/`.

#### Scenario: Sent destination already exists
- **WHEN** a pending file has been published successfully
- **AND** `outbox/sent/<filename>` already exists
- **THEN** `MarkSent` SHALL move the source to a collision-free sent filename or apply a documented overwrite policy
- **AND** it SHALL NOT return failure solely because the original sent destination name exists

#### Scenario: Legacy pending file collides with sent history
- **WHEN** a legacy pending file such as `params_0.jsonl` or `params_<epoch>.jsonl` is published
- **AND** the same filename already exists in `outbox/sent/`
- **THEN** the pending file SHALL be removed from pending through a successful collision-safe sent transition
- **AND** future publish cycles SHALL NOT repeatedly retry that same file because of `errno=17`

### Requirement: Outbox synchronization SHALL cover file state transitions
Outbox synchronization SHALL protect active filename state and filesystem transitions that change upload eligibility.

#### Scenario: Active path selected for append
- **WHEN** `AppendParameter` selects the active file path for a payload
- **THEN** no network operation SHALL be able to seal, publish, move, or delete that same active file until the append operation has completed or the implementation has otherwise made the append target independent of uploadable sealed files

#### Scenario: Network scans sealed files
- **WHEN** the network task scans pending files
- **THEN** the scan result SHALL contain only immutable sealed files and immutable failure files
- **AND** active files SHALL be excluded by filename state and by lifecycle state

