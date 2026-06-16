## Context

Runtime verification log `build/log/idf_py_stdout_output_31944` shows:

- NVS is initialized before monitor calibration access. This satisfies the order check for task 5.4.
- SDMMC debug lines with `error 0x107` occur during normal SD-card probing and are followed by successful card init. These are not fatal application errors.
- Outbox rename fails with `errno=17` while moving `pending/params_*.jsonl` to `sent/params_*.jsonl`. On FATFS this maps to a destination-exists collision.
- Failure-triggered publish cycles publish parameter files too, including `params_0.jsonl`, before cadence verification can pass.
- `network_task` stack overflows after a multi-file publish batch.

The previous review also found that the active parameter file can still race with network sealing/publishing because the logger releases the outbox mutex before the append write completes, and network batch selection does not filter parameter files when a failure file made the cycle urgent.

## Goals / Non-Goals

**Goals:**

- Make outbox parameter file lifecycle explicit: active, sealed, publishing, sent.
- Remove filename collisions in both pending and sent directories.
- Prevent invalid-time `params_0.jsonl` from becoming a recurring collision point.
- Keep failure upload urgent without dragging parameter files ahead of cadence.
- Make network publish path stack-safe under the observed multi-file MQTT workload.
- Centralize NVS initialization ownership and avoid hidden re-init/erase calls.
- Add verification steps for observed runtime failures and log classification.

**Non-Goals:**

- No MQTT payload schema change.
- No change to configured publish cadence semantics.
- No switch away from SD-backed JSONL outbox.
- No deep-sleep or power-policy redesign.

## Decisions

### Use explicit active and sealed parameter names

Parameter appends should target an active filename that is never returned by upload scans, for example `params_active_<boot_seq>.jsonl` or another reserved active pattern. When cadence is due, sealing should atomically rename the active file to an immutable upload filename. After sealing, new appends create a new active file.

Alternative considered: keep `params_<epoch>.jsonl` active and track only `s_current_params_file`. That already failed because path selection and file writes are not atomic with network scans, and same epoch names can be recreated after a seal.

### Avoid wall-clock-only file identity

Outbox filenames should include either a monotonic boot sequence, persisted counter, or microsecond timestamp fallback so files remain unique when wall-clock time is invalid. Wall-clock epoch may remain part of the name for readability after SNTP sync, but it must not be the only collision-avoidance field.

Alternative considered: keep `params_0.jsonl` until SNTP sync. The log shows `params_0.jsonl` survived to upload and collided with existing sent history.

### Make sent moves collision-safe

`MarkSent` should not fail a successful MQTT publish only because `sent/<filename>` exists. Options:

- Prefer unique sealed filenames so collisions should not occur.
- Still make `MarkSent` defensive: if destination exists, move to a suffixed sent name or replace only after verifying policy allows overwrite.

Do not leave the source in pending after successful publish solely due to `EEXIST`; that poisons future cycles and drives backoff.

### Filter publish batches by reason

Network task should choose an allowed file class before connecting:

```
pending files
   |
   +-- failure exists and backoff clear -> publish failures now
   |                                    -> publish params only if cadence due
   |
   +-- no failure and params due       -> seal and publish sealed params
   |
   +-- no failure and params not due   -> skip cadence
```

This keeps failure files urgent and parameter files periodic.

### Reduce network task automatic storage

The observed stack overflow occurred after processing several files with MQTT active. The network task should avoid large local arrays in recurring call paths. Candidate changes:

- Move `FileEntry[kMaxPendingFiles]` to static task-owned storage or a small iterator API.
- Reduce `line_buf[1024]`, make it static task-owned, or stream smaller chunks.
- Add stack high-water logging around connect, publish batch, and cleanup.
- Increase `network_task` stack only after local-buffer reductions and with measured margin.

### Single-owner NVS init

`app_main` should own `nvs_flash_init()` and recovery erase. Components should assume NVS is initialized, or call a shared idempotent wrapper that never erases after startup. Network/logger helper functions should not maintain independent `s_nvs_initialized` flags that can diverge from actual system state.

## Risks / Trade-offs

- Filename migration risk -> Existing `pending/params_*.jsonl` files from older firmware must be treated as sealed and uploaded.
- More file renames -> FATFS rename behavior must be checked for destination-exists and same-directory atomicity.
- Static network buffers increase BSS -> Validate `idf.py size` and heap margin after change.
- Stack increase may hide buffer problems -> Treat stack bump as final tuning, not primary fix.
- NVS centralization may affect raw logger or alternate app entry points -> Preserve backward-compatible init wrappers that call the shared initializer.

## Migration Plan

1. Teach outbox scanner to classify legacy `params_*.jsonl` files as sealed when they are not the active reserved name.
2. Add collision-safe sent move behavior before changing naming, so existing SD contents stop causing repeated backoff.
3. Introduce active-to-sealed parameter lifecycle and unique sealed filenames.
4. Filter network batches by allowed file class and cadence.
5. Reduce network stack pressure and record high-water marks.
6. Centralize NVS initialization while preserving compatibility wrappers.
7. Re-run build, test build, size, diff check, and hardware verification logs.

Rollback: old pending files remain JSONL. If new naming must be reverted, scanner can continue to treat any recognized `params_*` sealed filename as uploadable.
