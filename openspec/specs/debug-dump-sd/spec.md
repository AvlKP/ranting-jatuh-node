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
   3. `MODAL_TIME_US,<elapsed_us>`
   4. `DECAY,R,<start_phys_idx>,<count>` (roll decay region)
   5. `DECAY,P,<start_phys_idx>,<count>` (pitch decay region)
   6. `RESULT,R,<freq_hz>,<zeta>` (ESP32 roll results)
   7. `RESULT,P,<freq_hz>,<zeta>` (ESP32 pitch results)
   8. `PEAKS,R,<n>,<amp0>,<t0>,<amp1>,<t1>,...` (roll peak envelope)
   9. `PEAKS,P,<n>,<amp0>,<t0>,<amp1>,<t1>,...` (pitch peak envelope)
   10. `COLLAPSED,R,<count>` (roll collapsed extrema count)
   11. `COLLAPSED,P,<count>` (pitch collapsed extrema count)
   12. `PAIRS,R,<n>,<center_idx0>,<center_val0>,<amp0>,<time0>,...` (roll centerline pairs)
   13. `PAIRS,P,<n>,<center_idx0>,<center_val0>,<amp0>,<time0>,...` (pitch centerline pairs)
   14. `RAW,R,<v0>,<v1>,...,<vN>` (full roll buffer, logical order)
   15. `RAW,P,<v0>,<v1>,...,<vN>` (full pitch buffer, logical order)
   16. `<<<END`

#### Scenario: MODAL_TIME_US format
- **WHEN** `MODAL_TIME_US` is written
- **THEN** `<elapsed_us>` SHALL be the integer microseconds spent in `AnalyzeModalAxis()` as measured by `esp_timer_get_time()`

#### Scenario: COLLAPSED format
- **WHEN** `COLLAPSED` is written
- **THEN** `<count>` SHALL be the number of lobe-collapsed extrema for that axis

#### Scenario: PAIRS format
- **WHEN** `PAIRS` is written
- **THEN** `<n>` SHALL be the number of centerline pairs
- **THEN** each quartet SHALL be `<center_logical_index>,<center_value>,<amplitude>,<time_s>`
- **THEN** `<center_logical_index>` SHALL be the logical buffer index of the pair's center point
- **THEN** `<center_value>` SHALL be the mean of the peak and trough values in degrees
- **THEN** `<amplitude>` SHALL be the half peak-to-peak amplitude in degrees
- **THEN** `<time_s>` SHALL be the time in seconds of the center point

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

### Requirement: Debug Dump Centerline Data

When `CONFIG_MONITOR_DEBUG_DUMP` is enabled and centerline modal analysis produces valid results, the dump SHALL include centerline pair and collapsed extrema data.

#### Scenario: Centerline data included on successful analysis
- **WHEN** `AnalyzeModalAxis()` produces valid collapsed extrema for an axis
- **WHEN** at least one centerline pair is constructed
- **THEN** `COLLAPSED,<axis>,<count>` SHALL be written with the collapsed extrema count
- **THEN** `PAIRS,<axis>,<n>,...` SHALL be written with all centerline pairs

#### Scenario: No valid centerline pairs
- **WHEN** `AnalyzeModalAxis()` produces zero centerline pairs for an axis
- **THEN** `COLLAPSED,<axis>,0` SHALL be written
- **THEN** `PAIRS,<axis>,0` SHALL be written (no pair data after count)

