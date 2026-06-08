## ADDED Requirements

### Requirement: Parse New Dump Tags

The Python analysis script SHALL parse the `MODAL_TIME_US`, `COLLAPSED`, and `PAIRS` tags from firmware debug dumps in addition to the existing META/DECAY/RESULT/PEAKS/RAW tags.

#### Scenario: Parse MODAL_TIME_US
- **WHEN** `_parse_single_snapshot()` reads a line with tag `MODAL_TIME_US`
- **THEN** the modal elapsed time in microseconds SHALL be stored in `DebugSnapshot.modal_elapsed_us`

#### Scenario: Parse COLLAPSED tag
- **WHEN** `_parse_single_snapshot()` reads a line with tag `COLLAPSED`
- **THEN** the collapsed extrema count for that axis SHALL be stored in `DebugSnapshot.roll_collapsed_count` (axis R) or `DebugSnapshot.pitch_collapsed_count` (axis P)

#### Scenario: Parse PAIRS tag
- **WHEN** `_parse_single_snapshot()` reads a line with tag `PAIRS`
- **THEN** the centerline pair data SHALL be parsed into per-pair fields (center_logical_index, center_value, amplitude, time_s) and stored in the appropriate axis list
- **THEN** each pair SHALL be stored as a structured object with all 4 fields

#### Scenario: Old dumps without new tags
- **WHEN** a debug dump file does not contain MODAL_TIME_US, COLLAPSED, or PAIRS tags
- **THEN** the corresponding fields in DebugSnapshot SHALL default to zero/empty
- **THEN** the script SHALL process the dump without errors

### Requirement: JSON Output Includes Centerline Data

The `--json` output SHALL include firmware centerline pair data, collapsed extrema counts, and modal analysis timing for each snapshot.

#### Scenario: JSON structure with centerline fields
- **WHEN** `generate_json()` produces JSON output for a snapshot with parsed centerline data
- **THEN** the output SHALL include `modal_elapsed_us` at the snapshot level
- **THEN** each axis object SHALL include `collapsed_count` and `centerline_pairs` (array of pairs with center_logical_index, center_value, amplitude, time_s)

#### Scenario: JSON with missing centerline data
- **WHEN** a snapshot has no parsed centerline data (old dump format)
- **THEN** `modal_elapsed_us` SHALL be 0
- **THEN** `collapsed_count` SHALL be 0
- **THEN** `centerline_pairs` SHALL be an empty array

### Requirement: Python Centerline-Corrected Frequency Recomposition

The Python script SHALL recompute natural frequency on the centerline-corrected residual signal by subtracting the firmware's interpolated centerline from raw data before Welch PSD, mirroring the firmware's `AnalyzeModalAxis()` pipeline.

#### Scenario: Centerline-corrected PSD
- **WHEN** `recompute_frequency()` is called with available centerline pair data
- **THEN** the script SHALL construct a piecewise-linear centerline from the firmware's pairs
- **THEN** the script SHALL subtract the centerline from the raw tilt to produce the residual
- **THEN** the script SHALL compute Welch PSD on the residual decay segment only
- **THEN** the peak frequency from the residual PSD SHALL be returned

#### Scenario: No centerline pairs available
- **WHEN** centerline pairs are empty for an axis
- **THEN** `recompute_frequency()` SHALL fall back to the existing raw-data Welch PSD path
- **THEN** the script SHALL report a diagnosis that centerline correction was not applied

### Requirement: Python Centerline-Corrected Damping Regression

The Python script SHALL compute damping regression from the firmware's pair-envelope peak amplitudes and times, matching the firmware's `ComputeDampingRegression()` behavior.

#### Scenario: Damping from centerline pair envelope
- **WHEN** the script computes damping for an axis
- **THEN** it SHALL use the firmware's exported pair envelope peaks (PEAKS tag) and centerline-corrected natural frequency
- **THEN** it SHALL perform log-linear regression on ln(amplitude) vs time
- **THEN** damping ratio SHALL be |slope| / (2π * natural_freq_hz)

### Requirement: Plot Comparison Includes Centerline Stats

The `--plot` comparison table SHALL include centerline statistics alongside existing ESP32 vs Python frequency/damping comparison.

#### Scenario: Centerline row in comparison table
- **WHEN** `generate_plot()` creates the ESP32 vs Python comparison table
- **THEN** the table SHALL include a row for collapsed extrema count and centerline pair count per axis
- **THEN** the table SHALL include a row for modal analysis elapsed time
- **THEN** the table SHALL include a row showing whether Python used centerline-corrected or raw-data FFT path

#### Scenario: No centerline data available
- **WHEN** a snapshot has no centerline data
- **THEN** the comparison table SHALL show "N/A" or "0" for centerline stat rows
- **THEN** the table SHALL indicate that Python used the raw-data FFT fallback path
