## Context

Debug CSV logging was introduced in commit `027373b` alongside posthoc-decay-analysis changes. The two features are interleaved in the same commit, making a simple `git revert` impossible without also reverting posthoc-decay-analysis.

The debug CSV feature spans the monitor-to-logger boundary:
- Monitor posts `MONITOR_EVENT_STREAM_SAMPLE` events at IMU sample rate (26–208 Hz)
- Logger receives events, formats CSV rows, buffers them in a ring buffer
- A 1 Hz periodic flush writes buffered rows to `debug.csv` on SD card
- All code is gated behind `CONFIG_APP_DEBUG_CSV_LOGS` (disabled by default)

The unstaged working tree also contains a second revision of debug-csv-logs (buffered flush, `state` column) which will be discarded via `git restore` before surgical removal begins.

## Goals / Non-Goals

**Goals:**
- Remove all debug CSV logging code from logger, monitor, and main components
- Remove `CONFIG_APP_DEBUG_CSV_LOGS` and related Kconfig entries
- Delete `debug-csv-logs` spec from `openspec/specs/`
- Delete `add-debug-csv-logs` archive directories from `openspec/changes/archive/`
- Preserve all posthoc-decay-analysis code intact

**Non-Goals:**
- Not modifying posthoc-decay-analysis, mqtt-node-topic-prefix, update-readme, or any other feature
- Not modifying the FSM, parameter computation, or SD card parameter/failure logging
- Not touching dashboard or any other component

## Decisions

### Decision 1: Surgical line-by-line removal instead of revert + re-apply

**Rationale:** `git revert 027373b` would also revert posthoc-decay-analysis. Re-applying posthoc changes manually after revert is riskier than removing only the unwanted lines.

**Alternative considered:** Interactive rebase to split the commit. Rejected because it rewrites history and the commit is already pushed.

### Decision 2: Discard unstaged changes before removal

**Rationale:** The working tree has unstaged changes from a buggy follow-up commit (including the second revision of debug-csv-logs with ring buffer and state column). Restoring to HEAD (`git restore .`) gives a clean baseline at the committed first-revision debug-csv-logs code, which is then surgically removed.

**Risk:** Unstaged changes for mqtt-node-topic-prefix and update-readme are lost. These will be re-implemented from their archive specs later.

### Decision 3: Removal order

1. Discard all unstaged changes (`git restore .`)
2. Remove debug-csv-logs from each source file (logger, monitor, main)
3. Delete OpenSpec spec and archive directories
4. Commit the removal as a single atomic commit

## Risks / Trade-offs

- **Lost mqtt/readme code** → Mitigation: Archive specs remain intact; features can be re-implemented from specs.
- **Missed debug-csv code** → Mitigation: Use grep for `DEBUG_CSV`, `StreamSample`, `debug.csv`, `AppendDebugLog`, `ResetDebugLog`, `SetDebugMonitor` across entire codebase to verify complete removal.
- **Compilation breakage** → Mitigation: After removal, verify compilation succeeds before committing.
