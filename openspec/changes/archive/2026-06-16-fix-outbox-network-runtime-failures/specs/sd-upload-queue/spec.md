## ADDED Requirements

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
