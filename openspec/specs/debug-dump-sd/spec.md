# debug-dump-sd Specification

## Purpose
TBD - created by archiving change debug-dump-sd. Update Purpose after archive.
## Requirements
### Requirement: Debug Dump Kconfig Toggle

The monitor SHALL provide a Kconfig boolean `CONFIG_MONITOR_DEBUG_DUMP` (default disabled) that gates all debug dump functionality. When disabled, no debug code SHALL be compiled.

#### Scenario: Debug dump disabled (default)
- **WHEN** `CONFIG_MONITOR_DEBUG_DUMP` is not set (default)
- **THEN** no debug dump code SHALL be included in the compiled binary
- **THEN** no SD card writes for debug data SHALL occur

#### Scenario: Debug dump enabled
- **WHEN** `CONFIG_MONITOR_DEBUG_DUMP` is set to `y`
- **THEN** `DumpDebugToSD()` SHALL be compiled and callable

### Requirement: Debug Dump Trigger

When `CONFIG_MONITOR_DEBUG_DUMP` is enabled, the monitor SHALL write a debug snapshot to SD card on each DISTURBED→IDLE transition where `is_exit` is true, immediately after FFT and damping computation complete.

#### Scenario: DISTURBED→IDLE exit triggers dump
- **WHEN** `ComputeAndPublish()` is called with `is_exit=true` and `CONFIG_MONITOR_DEBUG_DUMP` is enabled
- **THEN** `DumpDebugToSD()` SHALL be called after decay analysis and before event posting
- **THEN** the dump SHALL include all intermediate computation data from that transition

#### Scenario: Buffer refresh does not trigger dump
- **WHEN** `ComputeAndPublish()` is called with `is_exit=false` (mid-DISTURBED buffer refresh)
- **THEN** no debug dump SHALL occur

#### Scenario: SD card unavailable
- **WHEN** `DumpDebugToSD()` attempts to open the debug file and `fopen` returns NULL
- **THEN** the function SHALL return silently without error
- **THEN** the DISTURBED→IDLE transition SHALL proceed normally

### Requirement: Debug Dump File Format

The debug dump file SHALL be written to `/sdcard/dbg_dump.csv` in append mode. Each snapshot SHALL be delimited by `>>>SNAPSHOT` (start) and `<<<END` (end) markers.

#### Scenario: Snapshot structure
- **WHEN** a debug snapshot is written
- **THEN** the file SHALL contain the following tagged lines in order:
  1. `>>>SNAPSHOT`
  2. `META,<timestamp_us>,<sample_count>,<rate_hz>`
  3. `DECAY,R,<start_phys_idx>,<count>` (roll decay region)
  4. `DECAY,P,<start_phys_idx>,<count>` (pitch decay region)
  5. `RESULT,R,<freq_hz>,<zeta>` (ESP32 roll results)
  6. `RESULT,P,<freq_hz>,<zeta>` (ESP32 pitch results)
  7. `PEAKS,R,<n>,<amp0>,<t0>,<amp1>,<t1>,...` (roll peak envelope)
  8. `PEAKS,P,<n>,<amp0>,<t0>,<amp1>,<t1>,...` (pitch peak envelope)
  9. `RAW,R,<v0>,<v1>,...,<vN>` (full roll buffer, logical order)
  10. `RAW,P,<v0>,<v1>,...,<vN>` (full pitch buffer, logical order)
  11. `<<<END`

#### Scenario: Raw data ordering
- **WHEN** raw buffer data is written to the RAW lines
- **THEN** samples SHALL be written in logical (chronological) order using `PhysicalIndex()` mapping
- **THEN** the number of values on each RAW line SHALL equal `sample_count` from the META line

#### Scenario: Multiple snapshots in file
- **WHEN** multiple DISTURBED→IDLE transitions occur
- **THEN** each snapshot SHALL be appended to the same file
- **THEN** each snapshot SHALL be independently parseable by its `>>>SNAPSHOT` / `<<<END` delimiters

### Requirement: Debug Analysis Python Script

A Python script SHALL be provided at `scripts/debug_analyze.py` that parses the SD card debug dump file and produces both human-readable visualization and machine-readable analysis output.

#### Scenario: Parse and visualize
- **WHEN** the script is run with `python debug_analyze.py <dump_file> --plot`
- **THEN** it SHALL parse all snapshots from the file
- **THEN** it SHALL generate matplotlib plots for each snapshot showing:
  - Raw tilt signal (roll and pitch vs time) with decay region highlighted
  - PSD computed via `scipy.signal.welch` with peak frequency annotated
  - Peak envelope with log-linear regression fit overlaid
  - Comparison table: ESP32 vs Python computed frequency and damping

#### Scenario: Machine-readable output
- **WHEN** the script is run with `python debug_analyze.py <dump_file> --json`
- **THEN** it SHALL output JSON to stdout containing per-snapshot analysis:
  - ESP32 results (freq, zeta) vs Python-recomputed results
  - Match/mismatch flags
  - Decay region sample count and duration
  - Diagnosis string identifying likely root cause of discrepancy

