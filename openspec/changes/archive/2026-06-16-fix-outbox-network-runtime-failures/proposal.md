## Why

Hardware verification for tasks 5.4-5.6 exposed runtime failures after the structure/safety change: SD outbox rename failures with `errno=17`, early parameter uploads during failure cycles, and a `network_task` stack overflow. These confirm the previous review finding that outbox file lifecycle and network publish selection are still unsafe under real WiFi/MQTT/SD load.

## What Changes

- Fix SD outbox file lifecycle so active parameter files cannot be uploaded, renamed, deleted, or recreated while the logger can append to the same logical file.
- Make parameter file sealing use an immutable file identity, preferably by renaming an active temp file to a sealed filename before upload eligibility.
- Make `MarkSent` handle `sent/` destination collisions deterministically, so duplicate historical filenames do not cause `errno=17` publish failures.
- Avoid invalid-time `params_0.jsonl` collisions by using boot-unique or sequence-based names until wall-clock time is valid.
- Preserve urgent failure uploads while preventing failure-triggered publish cycles from uploading parameter files before the configured cadence.
- Reduce `network_task` stack pressure by moving large recurring buffers out of automatic storage, chunking file reads, or increasing stack only with measured margin.
- Centralize NVS initialization ownership so network/logger helpers do not re-initialize or erase NVS after `app_main` has initialized it.
- Add verification requirements for runtime logs: NVS order, sealed-file-only parameter upload, urgent failure upload, no `errno=17`, and no stack overflow.
- Clean diff hygiene for changed files so whitespace checks pass before implementation is considered complete.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `sd-upload-queue`: strengthen active/sealed/sent file lifecycle, collision handling, and invalid-time parameter naming.
- `network-task`: require cadence-filtered publish batches and stack-safe publish processing.
- `embedded-runtime-safety`: require network task stack margin during real publish cycles and clean diff hygiene.
- `imu-calibration`: clarify single-owner NVS initialization before calibration and network/logger NVS users.

## Impact

- Affected components: `logger` outbox, `logger` network task, network strategy NVS helpers, monitor calibration startup path, and verification tasks.
- Public firmware APIs should remain compatible unless an internal outbox API must expose active/sealed file states for tests.
- Runtime behavior changes: failure files remain urgent, parameter files upload only when sealed and cadence due, sent file collisions no longer poison backoff, and network task must survive multi-file publish batches.
- Validation requires app build, monitor test build, size output, `git diff --check`, and hardware log evidence for tasks 5.4-5.6.
