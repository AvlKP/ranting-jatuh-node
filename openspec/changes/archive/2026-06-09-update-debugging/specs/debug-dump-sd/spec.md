## MODIFIED Requirements

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
  8. `PEAKS,R,<n>,<amp0>,<t0>,<amp1>,<t1>,...` (roll pair envelope)
  9. `PEAKS,P,<n>,<amp0>,<t0>,<amp1>,<t1>,...` (pitch pair envelope)
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

## ADDED Requirements

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
