## Why

`debug_analyze.py` silently ignores three new dump tags (`MODAL_TIME_US`, `COLLAPSED`, `PAIRS`) added by the `port-centerline-modal-analysis-to-firmware` change. The Python recomputation still uses old peak-based frequency/damping on raw data, but the firmware now computes these on centerline-corrected residual data. The analysis tool cannot validate the firmware's centerline modal results against notebook-derived values, making debug dumps less useful for cross-validation.

## What Changes

- **Parser**: Add parsing of `MODAL_TIME_US`, `COLLAPSED`, and `PAIRS` tags to `DebugSnapshot` dataclass and `_parse_single_snapshot()`
- **JSON output**: Include firmware centerline pairs, collapsed extrema counts, and modal timing in `--json` output
- **Plot**: Update ESP32 vs Python comparison table to show centerline pair counts and collapsed extrema statistics
- **Python recomputation**: Add centerline subtraction + residual FFT path that mirrors firmware's `AnalyzeModalAxis()` pipeline for cross-validation
- **Spec**: Update `debug-dump-sd` to document the new dump tags

## Capabilities

### New Capabilities
- `debug-analyzer-format`: Updated debug dump analysis tool that parses centerline modal tags and can replicate firmware's centerline-compensated FFT/damping pipeline in Python

### Modified Capabilities
- `debug-dump-sd`: File format requirements need to reflect the 3 new optional tags (`MODAL_TIME_US`, `COLLAPSED`, `PAIRS`) now written by the firmware

## Impact

- `scripts/debug_analyze.py` — parser, dataclass, JSON output, plot, and recomputation changes
- `openspec/specs/debug-dump-sd/spec.md` — add new tags to file format spec
