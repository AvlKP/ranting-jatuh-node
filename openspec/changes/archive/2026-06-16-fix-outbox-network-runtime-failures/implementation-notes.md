## Runtime Log Evidence

Original source log `build/log/idf_py_stdout_output_31944` was deleted. Replacement verification log:

`build/log/idf_py_stdout_output_38852`

### Error Classification

- Lines 207 and 209: `sdmmc_req: process_command_response: error 0x107`.
- Line 210: `sdmmc_io: sdmmc_init_io: io_send_op_cond (1) returned 0x107; not IO card`.
- Lines 211-217: SD card initialization continues as SD card, configures 1-bit bus, mounts FATFS driver.
- Line 218: `OUTBOX: Outbox initialized: /ranting/outbox`.

Classification: SDMMC `0x107` lines are benign IO-card probe failures because card init continues and outbox mounts.

### NVS And Calibration

- Line 184: `RUNTIME_NVS: NVS initialized`.
- Lines 185-187: `calib` namespace opened and `imu_bias` checked.
- Line 187: `APP: Calibration biases already stored; default write skipped`.
- Line 191: monitor opens `calib` namespace for read.
- Line 194: `MONITOR: Calibration bias read result: ESP_OK`.

Classification: NVS initializes before calibration access. Stored calibration is reused; hard-coded defaults are not rewritten.

### Outbox And Publish Runtime

- Lines 553-599 repeatedly show `Skipping publish: params_due=0 has_params=1 has_failure=0`.
- Line 796: active params sealed: `params_active_c5821e19_000001.jsonl -> params_1781620861_c5821e19_000002.jsonl`.
- Line 905: sealed params published after seal.
- Lines 963, 971, 1032, 1068, 1099, 1106: failure CSV events logged.
- Line 1094: urgent failure file published.
- Lines 1271, 1273, 1275, 1277, 1279: five failure files published in one batch.
- Lines 498, 722, 908, 1114, 1282: publish batches succeeded.
- No `OUTBOX errno=17`, `MarkSent failed`, `MQTT publish failed`, `Backoff`, panic, stack canary, or stack overflow lines appear in this log.

Classification: failure files publish urgently when a network cycle runs; parameter files remain pending before cadence and publish only after sealing/cadence. Sent transitions no longer show destination-exists failures.

### Stack Margin

`network_task` high-water lines:

- Start: line 344, 3884 bytes.
- First multi-file batch: pre-publish line 482, 3548 bytes; post-publish line 497, 2956 bytes; post-cleanup line 551, 1436 bytes.
- Five-file failure batch: pre-publish line 1270, 1436 bytes; post-publish line 1281, 1436 bytes; post-cleanup line 1334, 1436 bytes.

Measured minimum high-water margin: 1436 bytes.

Decision: keep `network_task` stack at 6144 bytes. Margin is above 1024-byte practical threshold after buffer reductions and multi-file publish/cleanup.

### POSIX/FATFS Collision Mapping

`errno=17` maps to `EEXIST`, destination exists. In the previous failure mode this matched pending-to-sent rename collision (`sent/<filename>` already present). Current verification log has no `errno=17` or stuck sent-transition error.

### Implementation Summary

- Active parameter files now use `params_active_*` names excluded from upload scans.
- Parameter files are sealed into immutable `params_<epoch>_<boot>_<seq>.jsonl` names only when cadence is due.
- Failure files include boot and sequence identity to avoid same-second collisions.
- Sent transitions choose collision-free sent filenames instead of failing solely on an existing destination.
- Network batches filter parameter files out of urgent failure cycles until cadence is due.
- Network publish buffers moved from recurring stack storage to task-owned static storage, with stack high-water logs around connect, publish, and cleanup.
- NVS init now routes through shared `runtime::EnsureNvsInitialized()` before calibration/logger/network storage access.
