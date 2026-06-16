## ADDED Requirements

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
