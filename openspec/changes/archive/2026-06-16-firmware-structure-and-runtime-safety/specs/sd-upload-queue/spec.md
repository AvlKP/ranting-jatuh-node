## ADDED Requirements

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
