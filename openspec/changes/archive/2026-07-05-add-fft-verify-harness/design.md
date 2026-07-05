## Context

The node computes branch natural frequency via `ComputeSignedAxisNaturalFrequency()` (`components/monitor/modal_analyzer.cpp:319`), a windowed, zero-padded, in-band power-argmax FFT on the dominant signed-gyro axis. In firmware it is only reachable through `AnalyzeImuEvent()` (`modal_analyzer.cpp:522`), which gates it behind `FindDecayOnsetTkeo()` and the node state machine (DISTURBED→IDLE). The existing Unity test `test_monitor_modal.cpp:123` exercises the FFT in isolation by poking `Monitor` privates (`#define private public`) with synthetic sines, but it is coupled to the whole `Monitor` class and only proves synthetic-signal recovery — not behavior on real IMU data driven by a known mechanical input.

We need to verify the FFT against a **known imposed oscillation frequency** on real hardware, scoped to the **active disturbance** (the live driven motion), with no free-decay-phase gating. A sibling standalone ESP-IDF project is the right vehicle: it copies the node's acquisition + FFT code verbatim (so it verifies the actual firmware algorithm) but drops the onset/state-machine/damping/dashboard machinery that would otherwise disturb the measurement.

Constraints (from AGENTS.md and repo): ESP-IDF v5.5.4, target ESP32-S3FH4R2 on the existing custom PCB, C++ exceptions disabled, `esp_err_t` propagation, PowerShell host, no heap in hot paths.

## Goals / Non-Goals

**Goals:**
- Standalone sibling ESP-IDF project at `../ranting-jatuh-fft-verify` that builds and flashes onto the same node board.
- Verify the node's natural-frequency FFT on live IMU data driven by a known mechanical oscillation.
- Scope the FFT to the **active disturbance**: a live, discrete, non-overlapping 5 s window (130 samples @ 26 Hz) of calibrated gyro — **not** the free-decay tail.
- Preserve the firmware FFT algorithm byte-for-byte: `SelectFftBinRange`, `ComputeDominantAxisSway`, `ComputeSignedAxisNaturalFrequency` logic identical (guards, Hann window `0.5-0.5cos(2π i/(count-1))`, 512/1024 fft_size select, zero-pad, ESP-DSP `dsps_fft2r_fc32`+`dsps_bit_rev_fc32`, in-band power `re²+im²` argmax, `max_bin*fs/fft_size`, `max_bin==0→0`).
- Preserve the firmware acquisition path: I2C 400 kHz on pins 5/6, addr 0x6A, LSM6DS3 ODR 26 Hz, gyro bias subtraction + lid-mount x/z sign-flip.
- Print `fn_hz`, dominant axis, and per-axis `sway_pp` each 5 s window via `ESP_LOGI` for manual comparison to the imposed frequency.

**Non-Goals:**
- No `FindDecayOnsetTkeo`, no node state machine, no DISTURBED/IDLE gating.
- No damping, no dashboard PSD, no MQTT, no SD, no logger.
- No automated oracle / PASS-FAIL check — operator compares printed `fn_hz` to the known imposed frequency.
- No changes to node firmware or its specs.
- No rolling/overlapping windows — discrete 5 s windows only.
- No support for non-default IMU rates — 26 Hz only (matches firmware default).

## Decisions

### D1: Sibling project, not a subdirectory of the node repo
**Choice:** `../ranting-jatuh-fft-verify`, a separate ESP-IDF project.
**Why:** The harness is a verification tool, not node firmware. Keeping it out of the repo avoids polluting the node's build/config/sdkconfig and keeps the node's OpenSpec specs clean. Copying (rather than referencing via `EXTRA_COMPONENT_DIRS`) means the harness is self-contained and flashes independently.
**Alternatives considered:**
- *Subdirectory in node repo* — rejected: drags harness into node build graph and sdkconfig.
- *`EXTRA_COMPONENT_DIRS` referencing node `Monitor`* — rejected: couples harness to whole `Monitor` (logger, dashboard, AE, mutexes) and requires `#define private public` to reach FFT internals; not standalone, fragile.

### D2: Copy-and-extract rather than link
**Choice:** Copy `lsm6ds3`, `pins.hpp`, `calibration.hpp`, I2C init verbatim; extract the three FFT functions into a standalone `fft_analyzer` module.
**Why:** Verbatim copy of acquisition + driver guarantees identical electrical/signal conditioning. Extracting the FFT functions (rather than copying all of `modal_analyzer.cpp`) removes the ring buffer, `Monitor` state, dashboard PSD, and config-member coupling so the harness exercises *only* the FFT algorithm on a linear buffer.
**Adaptations (logic-preserving):** ring `PhysicalIndex(start+i)` → linear `gx[start+i]`; `config_.modal_freq_min_hz/max_hz` → params `fmin/fmax`; `CONFIG_MONITOR_IMU_RATE_HZ` → `fs` param; `fft_input_` member → workspace param `float[2*kFftWindowSamples]`; drop `ClearDashboardPsd`/`PublishDashboardPsdFromFft` side-effect calls. Algorithm body unchanged.
**Alternatives:**
- *Copy entire `modal_analyzer.cpp` + `Monitor`* — rejected: pulls logger/dashboard/AE/state-machine deps, defeats "standalone."
- *Reimplement FFT from the doc* — rejected: verifies the spec, not the code.

### D3: Discrete non-overlapping 5 s windows
**Choice:** Collect 130 samples → FFT → print → reset → repeat. No overlap, no rolling.
**Why:** User decision. Clean independent windows; FFT runs once per 5 s (low CPU); readout is a steady ~5 s cadence matching each driven trial. Overlap would reuse samples across windows and complicate the "one trial = one number" comparison.
**Trade-off:** 5 s dead time between updates — acceptable since the operator imposes a steady oscillation and reads one result per trial.

### D4: Auto dominant axis, firmware-faithful
**Choice:** Copy `ComputeDominantAxisSway` and select the largest integrated peak-to-peak sway axis each window; FFT that axis only.
**Why:** User decision. Mirrors the firmware exactly, so the harness verifies the same axis-selection + FFT pipeline the node runs. Per-axis `sway_pp` is printed so the operator can see *why* an axis was chosen and confirm the imposed motion shows up where expected.
**Trade-off:** if the imposed motion is not on the dominant axis (e.g., a second axis carries more integrated drift), the reported frequency could be from the wrong axis. Mitigation: operator prints all three `sway_pp` and drives along a single known axis.

### D5: Calibration identical to node
**Choice:** Read gyro biases from NVS namespace `calib` key `imu_bias` (same as node `Monitor::Init`); fall back to hardcoded defaults `gx=1.096412, gy=-2.593744, gz=0.414028` if absent. Apply `calib_g = gyro - bias` with x/z sign-flip under `CONFIG_FFT_VERIFY_MOUNT_LID` mirroring `CONFIG_MONITOR_IMU_MOUNT_LID`.
**Why:** The FFT operates on calibrated gyro histories in firmware (`monitor.cpp:145-147`); the harness must condition the signal identically or it is verifying a different signal.
**Trade-off:** NVS shared with node firmware — if both write biases they could collide. Mitigation: harness only *reads* NVS (never writes); defaults match the node's `EnsureDefaultCalibrationBiases`.

### D6: Static FFT workspace, no heap in hot path
**Choice:** `static std::array<float, 2*kFftWindowSamples>` workspace; `dsps_fft2r_init_fc32(nullptr, 1024)` once at boot.
**Why:** Matches node's heap-free hot path and AGENTS.md §4 runtime-safety. `kFftWindowSamples=1024` copied verbatim.
**Alternatives:** heap allocation per window — rejected: fragmentation risk on constrained target.

### D7: Single sampler task, fixed rate
**Choice:** One FreeRTOS task, `vTaskDelayUntil` at `ceil(1000/26)` ms, core 1, priority 5 — mirroring `Monitor::TaskLoop` (`monitor_task.cpp:93`). Read IMU, calibrate, store into window; on 130th sample run FFT + print + reset.
**Why:** Identical cadence to firmware sampling; `vTaskDelayUntil` gives bounded jitter. Single task is the minimal harness.

## Risks / Trade-offs

- **[Code drift]** Node FFT algorithm changes; harness copy goes stale and verifies old behavior. → Mitigation: harness README records the source commit hash and the copied function line numbers; re-sync checklist references `docs/natural-frequency-pipeline.md` "Last verified" header convention.
- **[Wrong-axis confound]** Imposed motion not on the auto-selected dominant axis → reported `fn_hz` from a different axis than the operator expects. → Mitigation: print all three `sway_pp` each window; operator drives along a single axis and confirms it is dominant.
- **[Resolution limit]** 130 samples @ 26 Hz → Rayleigh resolution ~1/5 s = 0.2 Hz; bin spacing 0.0508 Hz (512-pt zero-pad). Reported precision exceeds true resolution; sub-bin interpolation is not in the firmware, so the harness intentionally omits it. → Mitigation: operator imposed frequency should differ from neighbors by >0.2 Hz; this is a property of the firmware algorithm under verification, not a harness bug.
- **[NVS bias collision]** Harness and node firmware share the `calib` NVS namespace. → Mitigation: harness reads only; never writes. Defaults match node defaults.
- **[Mount orientation mismatch]** If `CONFIG_FFT_VERIFY_MOUNT_LID` != node's `CONFIG_MONITOR_IMU_MOUNT_LID`, the x/z sign-flip diverges and axis labels swap. → Mitigation: harness Kconfig default mirrors node default; operator confirms setting before flashing.
- **[IMU read failure]** I2C read drops a sample mid-window. → Mitigation: on read failure, `ESP_LOGW` and skip; do not advance window index; window stays aligned to real elapsed time via `vTaskDelayUntil`.
