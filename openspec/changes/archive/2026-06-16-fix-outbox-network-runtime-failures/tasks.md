## 1. Reproduce And Classify Runtime Errors

- [x] 1.1 Extract relevant `err` and stack-overflow log lines from `build/log/idf_py_stdout_output_31944` into implementation notes.
- [x] 1.2 Classify SDMMC `0x107` debug probe lines as benign when followed by successful SD mount.
- [x] 1.3 Confirm `OUTBOX errno=17` maps to sent destination collision on the active filesystem.
- [x] 1.4 Confirm `network_task` stack overflow path occurs during multi-file publish cleanup or backoff handling.

## 2. Outbox File Lifecycle

- [x] 2.1 Introduce explicit active parameter filenames that are never returned by pending upload scans.
- [x] 2.2 Seal active parameter files into immutable upload filenames only when parameter cadence is due.
- [x] 2.3 Make sealed parameter filenames unique when wall-clock time is invalid or repeated.
- [x] 2.4 Make failure filenames unique for multiple failures in the same second.
- [x] 2.5 Make `MarkSent` handle existing sent destination names without leaving successfully published files in pending.
- [x] 2.6 Preserve upload support for legacy pending `params_*.jsonl` and `failure_*.jsonl` files already on SD.
- [x] 2.7 Ensure outbox synchronization covers active-file append selection and upload eligibility transitions.

## 3. Network Publish Selection And Stack Safety

- [x] 3.1 Filter publish batches so failure-triggered cycles publish only failure files when parameter cadence is not due.
- [x] 3.2 Allow sealed parameter files in the same cycle only when parameter cadence is due.
- [x] 3.3 Move large recurring network publish buffers out of automatic storage or reduce them with chunked processing.
- [x] 3.4 Add network task stack high-water diagnostics around connect, publish batch, and cleanup paths.
- [x] 3.5 Tune `network_task` stack size only after buffer changes and record measured margin.

## 4. NVS Ownership And Calibration Startup

- [x] 4.1 Create or reuse one shared idempotent NVS initializer for normal firmware startup.
- [x] 4.2 Remove independent network/logger NVS erase/reinit ownership from component helpers or route them through the shared initializer.
- [x] 4.3 Keep backward-compatible component init wrappers for alternate entry points.
- [x] 4.4 Avoid rewriting hard-coded calibration biases on every boot when stored biases already exist.
- [x] 4.5 Log calibration read/write outcomes so startup verification can prove NVS order.

## 5. Tests And Static Validation

- [x] 5.1 Add or update outbox tests for active exclusion, sealing, legacy pending files, invalid-time unique names, and sent collision handling.
- [x] 5.2 Add or update network-task tests or seams for failure-only batches before cadence and mixed batches after cadence.
- [x] 5.3 Run `idf.py build` successfully.
- [x] 5.4 Run `idf.py -B build-test -D TEST_COMPONENTS=monitor build` successfully.
- [x] 5.5 Run `idf.py size` and record app partition headroom.
- [x] 5.6 Run `git diff --check` successfully.

## 6. Hardware Runtime Verification

- [x] 6.1 Verify logs show NVS initialized before calibration `calib` namespace access.
- [x] 6.2 Verify logs show no repeated hard-coded calibration commit when stored calibration already exists.
- [x] 6.3 Verify failure files publish urgently when backoff allows.
- [x] 6.4 Verify parameter files publish only after sealing and only when cadence is due.
- [x] 6.5 Verify no `OUTBOX errno=17` occurs during sent transitions, including legacy-file upload.
- [x] 6.6 Verify no `network_task` stack overflow occurs during a multi-file publish batch.
- [x] 6.7 Record `network_task` stack high-water margin after publish success and publish-error/backoff paths.
- [x] 6.8 Document benign SDMMC `0x107` probe lines separately from application errors in verification notes.
