## Context

The monitor component computes natural frequency (Welch FFT) and damping ratio (log-linear regression on peak envelope) on DISTURBED→IDLE transitions. Results are consistently wrong: 0.051 Hz instead of >1 Hz physical oscillation. No intermediate data is observable — the computation is a black box between raw tilt buffer and final MonitorResult.

Current data flow:
```
roll/pitch buffer → FindDecayRegion() → decay subset
                                            ├→ ComputeAxisNaturalFrequency() → freq_hz
                                            └→ peak envelope → ComputeDampingRegression() → zeta
```

All intermediate state (decay region bounds, FFT input/output, peak list) is discarded after computation.

## Goals / Non-Goals

**Goals:**
- Dump all intermediate computation data to SD card on each DISTURBED→IDLE transition
- Format data for both human analysis (Python matplotlib) and AI agent analysis (structured parsing)
- Zero impact on existing real-time behavior when disabled
- Minimal code footprint in monitor component

**Non-Goals:**
- Fixing the frequency/damping bug (this change is diagnostic only)
- Real-time streaming of debug data
- Adding new dashboard pages or MQTT topics for debug
- Dumping data during IDLE or mid-DISTURBED states

## Decisions

### D1: SD Card File Output (not Serial or MQTT)

- **Choice**: Write debug dump to `/sdcard/dbg_dump.csv` using fprintf
- **Rationale**: SD card persists data for offline analysis. Serial output is ephemeral and unreadable for large data (hundreds of floats). MQTT would require serialization buffer allocation and is overkill for debug.
- **Alternatives considered**:
  - Serial (ESP_LOGI): Line length limit (~256 bytes), ephemeral, requires active capture. Rejected.
  - MQTT debug topic: Adds serialization complexity, buffer allocation, network dependency. Rejected for debug-only feature.

### D2: Synchronous Write from Monitor Task

- **Choice**: Call `DumpDebugToSD()` synchronously inside `ComputeAndPublish()` after FFT/damping computation completes, before returning.
- **Rationale**: DISTURBED→IDLE transition already blocks for FFT (~50ms). Adding ~50ms for SD write is acceptable because (a) transition is rare (minutes apart), (b) next samples are IDLE (low value), (c) avoids complexity of async write (separate task, data copy, signaling).
- **Alternatives considered**:
  - Async write via separate task: Requires copying full buffer (~60KB for 7800×2 floats) to transfer ownership. RAM cost too high. Rejected.
  - Post event to logger: Event payload too small for raw buffer data. Would need shared memory or pointer passing with lifetime issues. Rejected.

### D3: Dump Full DISTURBED Buffer + Decay Region Metadata

- **Choice**: Dump the entire roll_history_ and pitch_history_ arrays (up to kStorageSamples), plus mark decay region start/count so Python can extract the subset.
- **Rationale**: Python can then analyze both the full disturbance AND the specific decay region the ESP32 chose. Enables diagnosing whether FindDecayRegion selected the wrong subset.
- **Alternatives considered**:
  - Dump only decay region: Smaller file but can't verify decay region selection correctness. Rejected.

### D4: Tagged CSV Line Format

- **Choice**: Simple tagged lines: `TAG,field0,field1,...`. One snapshot delimited by `>>>SNAPSHOT` / `<<<END`.
- **Rationale**: Easy to emit from C (fprintf loop), easy to parse in Python (split by comma, switch on tag). No JSON serialization needed on ESP32.
- **Alternatives considered**:
  - JSON: Requires building JSON strings, escaping, buffer management. Overkill for debug. Rejected.
  - Binary format: Compact but hard to inspect manually. Rejected.

### D5: Append Mode with Snapshot Delimiters

- **Choice**: Single file `/sdcard/dbg_dump.csv`, append mode. Each snapshot delimited by `>>>SNAPSHOT` / `<<<END`.
- **Rationale**: Multiple snapshots in one file for comparative analysis. User manages file size manually (delete when full). Simpler than numbered files (no counter state to persist).

## Risks / Trade-offs

- [Risk] SD card not mounted → `fopen` returns NULL → Mitigation: silent skip, log warning once.
- [Risk] Write blocks monitor loop for ~50ms → Mitigation: only fires on rare DISTURBED→IDLE, next samples are IDLE (acceptable loss).
- [Risk] File grows unbounded → Mitigation: debug feature is temporary, user manages file. Not a production concern.
- [Trade-off] Synchronous write vs async complexity → Accepted: simplicity wins for a debug-only feature.

## Migration Plan

N/A — purely additive debug feature behind Kconfig toggle. No deployment changes, no API changes, no behavior changes when disabled.

## Open Questions

None — design fully explored in discovery session.
