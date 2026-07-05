## 1. Monitor PSD Producer

- [x] 1.1 Add a bounded monitor helper or write path that updates `psd_accum_` from computed positive-frequency FFT powers.
- [x] 1.2 In `ComputeSignedAxisNaturalFrequency()`, write 1024-point FFT powers directly into all 512 `psd_accum_` slots after successful FFT execution.
- [x] 1.3 In the 512-point FFT case, duplicate each of 256 positive-frequency bins into adjacent `psd_accum_` slots.
- [x] 1.4 Clear `psd_accum_` when FFT input is invalid, bin range is invalid, or ESP-DSP FFT execution fails.
- [x] 1.5 Ensure PSD buffer updates are synchronized without holding the monitor mutex across ESP-DSP FFT computation.

## 2. Dashboard Contract

- [x] 2.1 Keep `/api/status` `"fft"` response shape unchanged and verify it still downsampled 512 monitor PSD bins to 128 response values.
- [x] 2.2 Confirm browser-side FFT plot update path needs no schema changes and still redraws from `data.fft`.

## 3. Tests

- [x] 3.1 Add or update Unity coverage with a synthetic dominant-axis sinusoid that produces nonzero `GetFftData()` output after modal FFT analysis.
- [x] 3.2 Add coverage for invalid FFT input or failed analysis leaving dashboard-visible PSD all zero.
- [x] 3.3 Run `idf.py -B build-test -D TEST_COMPONENTS=monitor build`.

## 4. Validation

- [x] 4.1 Run `idf.py build`.
- [x] 4.2 Run `openspec validate fix-dashboard-fft-plot --strict`.
- [x] 4.3 On hardware, trigger a disturbance and confirm `/api/status` returns nonzero `"fft"` bins after DISTURBED->IDLE.
- [x] 4.4 Confirm dashboard FFT plot visibly changes without reloading the page.

## 5. Runtime Issues Found During Hardware Check

- [x] 5.1 Add section-level logging in `Dashboard::StatusHandler()` so `httpd_uri: uri handler execution failed` identifies the failing JSON/SD/MQTT chunk path.
- [x] 5.2 Fix persistent WiFi state handling so `EnsureConnected()` does not clear `kWifiConnectedBit` after a successful wait.
- [x] 5.3 Avoid restarting or reconfiguring STA in persistent mode when ESP WiFi is already connected.
- [x] 5.4 Rebuild and confirm serial log no longer shows false `NET_PERSIST: WiFi connect timeout` while dashboard remains reachable.
- [x] 5.5 Re-test dashboard during DISTURBED events and confirm `/api/status` polls do not fail, or logs identify exact failing section.
