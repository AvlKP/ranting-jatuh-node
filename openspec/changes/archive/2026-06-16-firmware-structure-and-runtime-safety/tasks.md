## 1. Baseline And Tests

- [x] 1.1 Replace unsupported Unity float comparison macros in monitor tests with supported assertions.
- [x] 1.2 Handle `[[nodiscard]]` monitor algorithm return values in tests without suppressing failures.
- [x] 1.3 Build app firmware with ESP-IDF and record size output before refactor.
  - Baseline: `ranting-jatuh-node.bin` size `0x10f230` bytes; app partition headroom `0x67dd0` bytes (28% free).
- [x] 1.4 Build monitor Unity target successfully before splitting implementation files.

## 2. Firmware Structure

- [x] 2.1 Split monitor task orchestration from algorithm implementation while keeping existing public monitor APIs stable.
- [x] 2.2 Move sample buffering and latest-sample snapshot logic into a dedicated monitor storage module.
- [x] 2.3 Move disturbance detection, modal analysis, and AE detection into dedicated monitor modules with direct unit-test seams.
- [x] 2.4 Split monitor-to-logger publishing code from monitor algorithm code.
- [x] 2.5 Update component CMake source lists for new files without changing component ownership boundaries.
- [x] 2.6 Add Doxygen summaries for new module boundaries and ownership contracts.

## 3. Runtime Safety

- [x] 3.1 Initialize NVS once before calibration, network, or logger code can access persistent storage.
- [x] 3.2 Make calibration writes commit to NVS and log read/write/commit failures with ESP-IDF error codes.
- [x] 3.3 Protect dashboard-visible monitor diagnostics with atomics or bounded critical-section snapshots.
- [x] 3.4 Ensure monitor snapshot APIs do not hold locks while doing I/O, logging, allocation, or other slow work.
- [x] 3.5 Replace heap-backed type-erased callbacks in IMU hot paths with fixed callback storage or static adapters.
- [x] 3.6 Keep any required heap allocation outside realtime sampling/publish paths and document ownership.
- [x] 3.7 Enforce documented LSM6DS3 reset delay before post-reset register access.

## 4. SD Outbox And Network Cadence

- [x] 4.1 Prevent network upload code from reading or moving the active parameter file.
- [x] 4.2 Seal parameter files before they become eligible for upload, including pending files found after reboot.
- [x] 4.3 Serialize shared outbox state with a static FreeRTOS mutex or equivalent bounded synchronization.
- [x] 4.4 Preserve failure files as immutable urgent upload records after creation.
- [x] 4.5 Make network notifications act as wake hints while final publish decisions honor backoff, strategy, and configured parameter cadence.
- [x] 4.6 Keep failure uploads urgent when backoff allows.
- [x] 4.7 Add diagnostics that distinguish skips caused by cadence, connectivity, backoff, missing sealed files, and upload errors.

## 5. Validation

- [x] 5.1 Run `idf.py build` successfully after implementation.
- [x] 5.2 Run `idf.py -B build-test -D TEST_COMPONENTS=monitor build` successfully after implementation.
- [x] 5.3 Run `idf.py size` and confirm partition headroom remains acceptable.
  - Final app binary size `0x10f810` bytes (baseline `0x10f230`); app partition headroom `0x677f0` bytes (28% free). Increase ~1.5 KiB. Acceptable.
- [x] 5.4 Verify runtime logs show NVS initialization before calibration access.
- [ ] 5.5 Verify runtime logs show parameter uploads only for sealed files at configured cadence. *(requires hardware runtime test)*
- [ ] 5.6 Verify runtime logs show failure uploads remain urgent when connectivity and backoff allow. *(requires hardware runtime test)*
