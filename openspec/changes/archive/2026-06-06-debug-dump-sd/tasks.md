## 1. Kconfig Toggle

- [x] 1.1 Add `CONFIG_MONITOR_DEBUG_DUMP` bool to `components/monitor/Kconfig` (default `n`, under Monitor menu).

## 2. Firmware Debug Dump

- [x] 2.1 Add `DumpDebugToSD()` private method declaration to `monitor.hpp`, guarded by `#if CONFIG_MONITOR_DEBUG_DUMP`.
- [x] 2.2 Implement `DumpDebugToSD()` in `monitor.cpp`. Parameters: decay regions (roll/pitch), peak lists (roll/pitch), computed results (freq/zeta for each axis). Function opens `/sdcard/dbg_dump.csv` in append mode, writes tagged CSV lines per spec, closes file. Silent return on fopen failure.
- [x] 2.3 Call `DumpDebugToSD()` from `ComputeAndPublish()` when `is_exit == true`, after decay analysis completes and before `esp_event_post`. Pass decay regions, peak lists, and computed frequency/damping values. Guard call with `#if CONFIG_MONITOR_DEBUG_DUMP`.

## 3. Python Analysis Script

- [x] 3.1 Create `scripts/debug_analyze.py` with parser that reads `dbg_dump.csv`, splits on `>>>SNAPSHOT`/`<<<END` delimiters, and parses each tagged line into a `DebugSnapshot` dataclass (metadata, decay regions, peaks, raw buffers, ESP32 results).
- [x] 3.2 Add `--plot` mode: for each snapshot, generate 4-subplot matplotlib figure (raw signal with decay region highlighted, scipy Welch PSD with peak annotated, peak envelope with regression line, ESP32 vs Python comparison table).
- [x] 3.3 Add `--json` mode: for each snapshot, recompute frequency (scipy Welch) and damping (log-linear regression on peaks), compare with ESP32 values, output structured JSON with match/mismatch flags and diagnosis.
- [x] 3.4 Add `--help` and argument parsing via argparse. Support positional dump file path, `--plot`, `--json`, and `--snapshot N` to select a specific snapshot index.

## 4. Verification

- [x] 4.1 Enable `CONFIG_MONITOR_DEBUG_DUMP`, flash to device, trigger a disturbance, verify `dbg_dump.csv` is created on SD card with correct format.
- [x] 4.2 Run `python scripts/debug_analyze.py dbg_dump.csv --plot` and verify plots render correctly.
- [x] 4.3 Run `python scripts/debug_analyze.py dbg_dump.csv --json` and verify JSON output is parseable and contains comparison data.
