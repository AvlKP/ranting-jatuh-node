## 1. Data Model Expansion

- [x] 1.1 Add `modal_elapsed_us: int` field to `DebugSnapshot` dataclass
- [x] 1.2 Add `roll_collapsed_count: int` and `pitch_collapsed_count: int` fields to `DebugSnapshot`
- [x] 1.3 Add `roll_centerline_pairs: List[CenterlinePair]` and `pitch_centerline_pairs: List[CenterlinePair]` fields to `DebugSnapshot`
- [x] 1.4 Create `CenterlinePair` dataclass with fields: `center_logical_index: int`, `center_value: float`, `amplitude: float`, `time_s: float`

## 2. Parser Updates

- [x] 2.1 Add parsing branch for `MODAL_TIME_US` tag in `_parse_single_snapshot()` — store elapsed microseconds
- [x] 2.2 Add parsing branch for `COLLAPSED` tag — store collapsed extrema count per axis
- [x] 2.3 Add parsing branch for `PAIRS` tag — parse variable-length pair quartet data into `CenterlinePair` list per axis
- [x] 2.4 Verify old dump files (no new tags) parse without errors — fields default to zero/empty

## 3. Python Centerline-Corrected Recomposition

- [x] 3.1 Implement `build_centerline(centerline_pairs, raw_length)` — construct piecewise-linear centerline from firmware pair data, using endpoint-hold for samples before first pair and after last pair
- [x] 3.2 Implement `subtract_centerline(raw_data, centerline)` — return residual signal
- [x] 3.3 Add `recompute_frequency_centerline()` — calls `build_centerline` + `subtract_centerline` + existing Welch PSD on residual decay segment
- [x] 3.4 Update `recompute_frequency()` to accept optional centerline pairs and dispatch to centerline-corrected path when available, fall back to raw-data Welch path when not

## 4. JSON Output Expansion

- [x] 4.1 Add `modal_elapsed_us` field to per-snapshot JSON output
- [x] 4.2 Add `collapsed_count` and `centerline_pairs` array (4 fields per pair) to each axis object in JSON output
- [x] 4.3 Add `centerline_corrected: bool` flag to each axis python section indicating whether centerline-corrected or raw-data FFT path was used
- [x] 4.4 Add `python_centerline_freq_hz` and `python_centerline_zeta` to each axis python section alongside existing raw-data values

## 5. Plot Updates

- [x] 5.1 Add centerline stats rows to comparison table: collapsed extrema count, centerline pair count, modal elapsed time per axis
- [x] 5.2 Add row indicating whether Python used centerline-corrected or raw-data FFT path
- [x] 5.3 Add centerline overlay trace on raw signal plot (dashed line) when centerline pairs are available
- [x] 5.4 Add residual signal sub-plot when centerline data is available (raw minus centerline)

## 6. Diagnostics Update

- [x] 6.1 Add diagnosis for centerline pair count < 4 (insufficient for damping regression)
- [x] 6.2 Add diagnosis for centerline-corrected freq vs raw-data freq mismatch (indicates centerline subtraction significantly changed the dominant frequency)
- [x] 6.3 Add diagnosis for centerline-corrected freq vs firmware RESULT freq mismatch > tolerance

## 7. Spec and Verification

- [x] 7.1 Verify `debug-dump-sd` delta spec correctly documents all new tags and their formats
- [x] 7.2 Test parsing on a real `dbg_dump.csv` with centerline data (from archived logs or new capture)
- [x] 7.3 Run `--json` and `--plot` on a dump with and without centerline tags and confirm both modes work
