## Why

The node's natural-frequency FFT (`ComputeSignedAxisNaturalFrequency`, `modal_analyzer.cpp:319`) is only exercised end-to-end through the decay-onset pipeline (`FindDecayOnsetTkeo` → `ComputeDominantAxisSway` → FFT), so FFT correctness cannot be verified in isolation from TKEO onset detection. A standalone live verifier is needed that FFTs the **active disturbance** (a live 5 s window of calibrated gyro) against a known, physically imposed oscillation frequency, with no decay-phase gating.

## What Changes

- New **sibling** ESP-IDF project `ranting-jatuh-fft-verify` (outside this repo, at `../ranting-jatuh-fft-verify`) that verifies the node's natural-frequency calculation on real hardware:
  - Copies `components/lsm6ds3`, `main/pins.hpp`, `components/monitor/include/calibration.hpp`, and the I2C init block (`InitImuI2c`/`ImuReadReg`/`ImuWriteReg`/`MapImuOdr`) **verbatim** from the node.
  - Extracts `SelectFftBinRange`, `ComputeDominantAxisSway`, and `ComputeSignedAxisNaturalFrequency` into a standalone `fft_analyzer` module: ring-buffer indexing replaced by linear buffer access, dashboard PSD side-effects (`ClearDashboardPsd`/`PublishDashboardPsdFromFft`) dropped, `config_` members passed as params. **Algorithm logic preserved identically** (guards, Hann window, zero-pad, ESP-DSP radix-2, in-band power argmax, bin→Hz conversion).
  - Samples the LSM6DS3 live at 26 Hz, applies identical calibration (gyro bias subtraction + lid-mount x/z sign-flip under `CONFIG_FFT_VERIFY_MOUNT_LID`), collects **discrete non-overlapping 5 s windows** (130 samples), runs dominant-axis selection + FFT, and prints `fn_hz` + dominant axis + per-axis sway_pp each window via `ESP_LOGI`.
  - No `FindDecayOnsetTkeo`, no node state machine, no damping, no dashboard, no MQTT, no SD.
- **No changes to node firmware.**

## Capabilities

### New Capabilities

- `fft-verification-harness`: A standalone, on-device ESP-IDF project that verifies the node's natural-frequency FFT calculation against a known imposed mechanical oscillation, scoped to the active disturbance (live 5 s window) rather than the free-decay phase.

### Modified Capabilities

_None._ No node-firmware spec requirements change; the harness copies and exercises existing algorithms without altering them.

## Impact

- **New project**: `../ranting-jatuh-fft-verify` (out-of-repo). No node code or config changes.
- **Copied code**: `lsm6ds3` component, `pins.hpp`, `calibration.hpp`, I2C init, and the three FFT/dominant-axis/bin-range functions. Divergence risk if the node changes — harness must be re-synced manually (documented in the harness README).
- **Dependencies**: `espressif/esp-dsp` managed component (for `dsps_fft2r_fc32` / `dsps_bit_rev_fc32` / `dsps_fft2r_init_fc32`); ESP-IDF v5.5.4; target ESP32-S3FH4R2 on the existing custom PCB.
- **Hardware**: runs on the same node board; flashed via `idf.py -p <port> flash monitor`.
- **Verification method**: manual — operator drives the node at a known oscillation frequency and compares the printed `fn_hz` to the imposed frequency.
- **Runtime safety**: no heap in the hot path (static FFT workspace); C++ exceptions disabled; `esp_err_t` propagation; matches AGENTS.md §4.
