## Context

The existing debug CSV system (`debug.csv`) logs raw stream samples (accel, tilt, state) every IMU sample at full rate, using a 256-entry atomic ring buffer in the monitor and a batched 1 Hz flush to SD card. FFT and damping calculations run only at DISTURBED→IDLE transitions — rare events (every few minutes at most) — and produce transient intermediate data (PSD arrays, peak lists) that is never persisted.

### Current Architecture
```
Monitor::Update()                     Logger task loop (1 Hz)
     │                                      │
     ├── debug_samples_[256] ───GetDebugSamples()──▶ format CSV ──▶ storage buffer[64] ──▶ debug.csv
     │  (every IMU sample)                   (batch of 32)
     │
     └── ComputeAndPublish(is_exit=true)
          ├── FindDecayRegion() → peaks[]  ← NOT captured
          └── ComputeAxisNaturalFrequency() → local_psd[] ← NOT captured
```

## Goals / Non-Goals

**Goals:**
- Capture per-bin PSD from both roll and pitch axis FFT at each analysis event
- Capture detected peak/trough envelope (time, amplitude, log-amplitude) for both axes
- Write to two new CSV files (`debug_fft.csv`, `debug_peaks.csv`) with the same gating, overwrite, and flush semantics as `debug.csv`
- Keep SD card writes infrequent (events are rare, batch-flush at 1 Hz)

**Non-Goals:**
- Real-time continuous FFT logging (PSD only changes at analysis events)
- Streaming the full FFT input buffer (1024 samples per axis — already available via `debug.csv` tilt columns)
- Changing the FFT or damping algorithm
- Adding new Kconfig options (reuse `CONFIG_APP_DEBUG_CSV_LOGS`)

## Decisions

### Decision 1: Per-bin rows vs wide-event rows

**Chosen**: Per-bin rows (`timestamp_ms,axis,bin,freq_hz,psd_power`)

**Rationale**: 512 bins × 2 axes × ~20 chars/row ≈ 30 KB per event. Wide-row would need 1024+ columns, variable-width peak columns, and is harder to parse. Per-bin rows are self-describing, append-friendly, and trivially pivotable in Python/pandas. The 1024-row batch per event is negligible for SD card throughput at the event frequency (minutes between events).

**Alternatives considered**:
- Wide-row CSV: rejected for variable column count and parsing complexity
- Binary format: rejected for debuggability (CSV is human-readable)
- JSON: rejected for ESP32 formatting overhead and file size

### Decision 2: Separate files vs combined file

**Chosen**: Two separate files (`debug_fft.csv`, `debug_peaks.csv`)

**Rationale**: PSD and peaks have different schemas and row counts. Combining them forces a discriminator column and makes filtering harder. Separate files mirror the existing pattern (`debug.csv`, `failure.csv` — each has its own file). Storage ring buffers are per-file anyway.

### Decision 3: Capture point in Monitor

**Chosen**: Capture in `ComputeAxisNaturalFrequency()` (PSD) and `FindDecayRegion()` (peaks), stored in new monitor members.

**Rationale**: These functions already produce the data. Copying to members avoids changing function signatures or adding callbacks. The logger drains them via the same ring-buffer pattern as debug_samples. Thread safety: a `std::mutex` or atomic flag protects the capture members (same as existing `mutex_` pattern).

**Alternatives considered**:
- Return PSD/peaks through `MonitorResult`: rejected — bloats the event struct, PSD is 2 KB
- Compute FFT again in logger: rejected — wasteful, doubles computation

### Decision 4: Flush strategy

**Chosen**: Immediate flush after each analysis event (all rows at once), not batched 1 Hz.

**Rationale**: Analysis events happen every few minutes. Batching adds complexity for no benefit — there won't be multiple events within a 1-second window. Each event produces ALL its rows atomically (one `fopen`/batch `fwrite`/`fclose` per file). No new ring buffers needed.

**Alternatives considered**:
- 1 Hz periodic flush: adds a ring buffer to hold PSD/peak rows between events; unnecessary given event frequency
- Per-row fopen/fclose: rejected for SD card wear (1000+ opens per event)

### Decision 5: Monitor-to-logger delivery

**Chosen**: Monitor writes PSD and peak data into shared buffers, sets a `has_new_analysis_data_` atomic flag. Logger checks flag at 1 Hz, reads data under mutex, formats CSV, flushes immediately.

**Rationale**: Simple, matches the existing `debug_samples_` ring buffer pattern conceptually. The data is small (~4 KB PSD + ~few hundred bytes peaks), so copying to logger stack for formatting is fine.

### Decision 6: File paths

**Chosen**: `/sdcard/debug_fft.csv` and `/sdcard/debug_peaks.csv` — same mount point pattern as existing debug files. Overwritten at boot via `ResetDebugLog()` equivalent functions.

## Risks / Trade-offs

- **Memory**: ~5 KB of new monitor members (PSD buffer + peak lists). Negligible on ESP32-S3 with PSRAM. → Acceptable.
- **SD card bandwidth**: ~35 KB per analysis event, events every few minutes. → Negligible.
- **Thread safety**: New capture members accessed from monitor task (write) and logger task (read). → Protected by existing `mutex_` or new atomic flag.
- **Data loss on crash**: Analysis data held in RAM until flush. Loss of at most one event's data on power loss. → Acceptable (debug data, not mission-critical).

## Open Questions

None — all decisions made during the exploration phase.
