# firmware-structure Specification

## Purpose
TBD - created by archiving change firmware-structure-and-runtime-safety. Update Purpose after archive.
## Requirements
### Requirement: Firmware modules SHALL be split by runtime responsibility
Large firmware components SHALL separate task orchestration, hardware access, persistent storage, signal-processing algorithms, and publication concerns into distinct translation units or internal modules.

#### Scenario: Monitor implementation is split by ownership
- **WHEN** the monitor component is built
- **THEN** FreeRTOS task orchestration, sample storage, disturbance detection, modal analysis, acoustic-emission handling, and event publication SHALL be implemented in separate focused source files or internal modules
- **AND** the public `Monitor` class SHALL remain a facade over those modules

#### Scenario: Logger network implementation is split by ownership
- **WHEN** the logger component is built
- **THEN** event dequeue/formatting, SD CSV storage, SD outbox ownership, MQTT publish orchestration, and WiFi strategy policy SHALL be separable by source file or internal module
- **AND** shared WiFi setup code SHALL NOT be duplicated between persistent and on-demand strategies except where policy differs

### Requirement: Public APIs SHALL stay stable during internal refactor
Internal module splits SHALL preserve existing application-facing APIs unless a spec explicitly changes the API.

#### Scenario: Existing app_main call sites compile
- **WHEN** `main.cpp` constructs and starts the monitor, logger, network task, and dashboard after the refactor
- **THEN** the normal firmware build SHALL compile without requiring behavior-only call-site rewrites

#### Scenario: MQTT and CSV payload schemas remain stable
- **WHEN** monitor results and failure events are logged or published after the refactor
- **THEN** existing CSV fields, JSON fields, and MQTT topics SHALL remain compatible with current documented schemas

### Requirement: Internal modules SHALL be directly testable
Algorithmic and stateful internals SHALL expose small internal interfaces that can be tested without constructing the full firmware task facade.

#### Scenario: Modal analysis tests avoid full Monitor private access
- **WHEN** tests validate decay onset, dominant-axis selection, natural-frequency estimation, or damping
- **THEN** tests SHALL be able to exercise the modal analysis module through an internal test-facing interface
- **AND** tests SHALL NOT need to access unrelated GPIO, ADC, task, or publication state

#### Scenario: Outbox tests avoid WiFi and MQTT
- **WHEN** tests validate outbox file sealing and file selection
- **THEN** tests SHALL run without initializing WiFi, MQTT, or the monitor component

### Requirement: Module boundaries SHALL preserve realtime constraints
Refactoring SHALL NOT introduce heap allocation, blocking I/O, network I/O, or unbounded loops into monitor sampling, ISR, ESP event handler, or logger enqueue paths.

#### Scenario: Monitor sample path remains bounded
- **WHEN** one monitor sample update executes after the refactor
- **THEN** it SHALL use fixed-capacity storage
- **AND** it SHALL NOT allocate heap memory
- **AND** it SHALL NOT perform file I/O or network I/O

#### Scenario: Event handlers remain bounded
- **WHEN** monitor result or failure events are delivered through the ESP event loop
- **THEN** each event handler SHALL copy bounded payloads and return without SD writes, MQTT publishes, DSP analysis, or blocking waits

