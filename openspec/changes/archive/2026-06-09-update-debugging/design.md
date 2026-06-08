## Context

`debug_analyze.py` was written before the centerline modal analysis pipeline existed in firmware. It parses META/DECAY/RESULT/PEAKS/RAW tags and recomputes frequency from raw tilt via Welch PSD. The firmware now computes frequency from centerline-corrected residual data, and the Python tool's comparison table compares ESP32 centerline-results against Python raw-data results — apples vs oranges. Three new dump tags (MODAL_TIME_US, COLLAPSED, PAIRS) are silently dropped.

## Goals / Non-Goals

**Goals:**
- Parse all 3 new dump tags into `DebugSnapshot` and expose in `--json` output
- Include centerline pair counts and collapsed extrema stats in `--plot` comparison table
- Add Python centerline subtraction + residual FFT path to match firmware's `AnalyzeModalAxis()` pipeline
- Verify Python residual-FTT frequencies match firmware's `RESULT` frequencies within tolerance
- Update `debug-dump-sd` spec to document the 3 new tags

**Non-Goals:**
- Full Python port of `CollapseExtremaLobes` and `BuildCenterlinePairs` — use firmware's exported COLLAPSED/PAIRS data instead
- Changes to firmware dump format or monitor.cpp logic
- Changes to SD card file format or delimiters

## Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Centerline replication strategy | Python imports firmware-collapsed extrema + centerline pairs from dump, then applies same SubtractCenterline + Welch FFT + damping regression | Avoids duplicating complex lobe-collapse logic in Python. The firmware already does extrema detection and lobe collapse — Python reads that output and replicates the post-collapse pipeline |
| New vs modified spec | New `debug-analyzer-format` spec for the analysis tool behavior; delta on `debug-dump-sd` for file format docs | The analysis tool is a distinct capability from the raw dump format. Keeping them separate avoids bloating the dump spec with analysis requirements |
| Backward compatibility | All 3 new tags are optional — old dumps without them still parse fine | `_parse_single_snapshot()` already drops unknown tags silently; new fields default to empty/zero if tags missing |

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| Python centerline interpolation doesn't match firmware's piecewise-linear exactly | Use firmware's centerline pair data directly (center_value, amplitude, time_s). Python only subtracts from raw and re-runs FFT — no interpolation risk |
| Firmware centerline pairs reference logical indices into circular buffer | Python must apply the same `PhysicalIndex()` mapping. The RAW data is already in logical order, so centerline indices can map directly to sample positions |
| Tolerance for freq/zeta match looser for centerline-corrected values | Expect tighter match since Python uses firmware's centerline directly; add diagnostic flag if centerline-corrected comparison shows worse agreement than old peak-based comparison |
