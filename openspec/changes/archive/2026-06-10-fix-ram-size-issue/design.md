## Context

The normal firmware build currently initializes SD, monitor storage, persistent WiFi, startup SNTP, dashboard HTTP server, logger task, then monitor task. A captured boot log shows WiFi association reaches `run` but never gets IP, startup SNTP times out, the dashboard starts, `logger_task` starts, then `xTaskCreatePinnedToCore()` fails for `monitor_task`.

`idf.py size` reports DIRAM at about 90% static usage. Runtime heap then pays for WiFi/lwIP buffers, HTTP server stack, logger queue/task stack, and monitor task stack. The fix must preserve deterministic monitor sampling and avoid heap use in the sampling hot path.

## Goals / Non-Goals

**Goals:**

- Ensure `logger_task` and `monitor_task` start reliably in the normal monitoring build with dashboard and WiFi enabled.
- Leave measurable internal heap margin after boot, not only enough bytes for one successful run.
- Emit actionable diagnostics when task creation fails: requested stack, free internal heap, and largest allocatable internal block.
- Reduce RAM pressure with bounded ESP-IDF configuration and stack sizing changes backed by high-water measurements.
- Preserve existing monitor sampling, SD logging, MQTT publishing, and dashboard endpoints.

**Non-Goals:**

- Fix WPA2-Enterprise authentication or AP handshake failures.
- Add PSRAM as a required dependency.
- Rewrite monitor algorithms or change logger payload formats.
- Add deep-sleep or power-state complexity.

## Decisions

1. Treat this as runtime heap pressure, not only binary size.

   `idf.py size` is necessary but insufficient because task stacks and WiFi/lwIP/HTTPD buffers are runtime allocations. Implementation will log boot heap checkpoints and largest internal allocatable block around WiFi init, dashboard start, logger start, and monitor start.

   Alternative considered: rely on static DIRAM size only. Rejected because the failure occurs at task creation after runtime subsystems allocate heap.

2. Keep monitor and logger task creation explicit, but improve failure reporting.

   Task start helpers will report `pdPASS` failure with configured stack size and heap diagnostics. This avoids silent `false` returns that hide whether the failure is stack, fragmentation, or other heap pressure.

   Alternative considered: use `ESP_ERROR_CHECK` or abort on task creation failure. Rejected because field logs should explain the failure and allow controlled boot failure behavior.

3. Reduce dynamic allocations before adding new hardware assumptions.

   Candidate reductions include dashboard HTTP server stack size, WiFi buffer counts, task stack sizes after high-water validation, and startup sequencing that avoids starting optional dashboard services before critical monitor/logger tasks. Each reduction must be verified with stack high-water output or ESP-IDF size/boot logs.

   Alternative considered: require PSRAM. Rejected because current target configuration has PSRAM disabled and monitor hot paths should remain internal-RAM deterministic.

4. Preserve no-heap monitor sampling.

   Large monitor history/scratch storage may be reorganized only if it remains bounded and does not allocate in the sample update path. Any lazy allocation must happen before `monitor_task` starts and fail with diagnostics.

   Alternative considered: allocate DSP scratch on demand during event analysis. Rejected unless allocation happens outside the recurring sample path and has explicit failure handling.

## Risks / Trade-offs

- Lower dashboard HTTPD stack too far -> stack canary panic under file listing/download. Mitigation: run dashboard status and file workflows, then record high-water margin.
- Lower WiFi buffers too far -> weaker throughput or unstable enterprise WiFi. Mitigation: keep MQTT payload sizes small, validate connect/publish under expected network, and prefer conservative buffer reductions.
- Reordering dashboard startup -> dashboard may appear later during boot. Mitigation: start critical monitor/logger tasks first and log dashboard start result separately.
- Heap fragmentation remains after reductions -> largest free block still too small. Mitigation: log largest internal free block at checkpoints and prefer static allocations or earlier task creation for long-lived stacks.
