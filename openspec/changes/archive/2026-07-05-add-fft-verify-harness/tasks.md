## 1. Scaffold sibling ESP-IDF project

- [x] 1.1 Create `../ranting-jatuh-fft-verify/` with top-level `CMakeLists.txt` (`include($ENV{IDF_PATH}/tools/cmake/project.cmake)` + `project(ranting-jatuh-fft-verify)`) matching node's boilerplate
- [x] 1.2 Add `sdkconfig.defaults` targeting ESP32-S3 (`CONFIG_IDF_TARGET="esp32s3"`, ESP-DSP enabled, PSRAM if node uses it, C++ exceptions disabled)
- [x] 1.3 Add `main/idf_component.yml` declaring `espressif/esp-dsp` managed dependency
- [x] 1.4 Add `main/CMakeLists.txt` with `idf_component_register(SRCS "main.cpp" "fft_analyzer.cpp" INCLUDE_DIRS "." REQUIRES "lsm6ds3" "esp-dsp")`
- [x] 1.5 Copy `components/lsm6ds3/` verbatim from node (single `lsm6ds3.cpp` + `include/`, `REQUIRES freertos`); add its `CMakeLists.txt`

## 2. Copy verbatim node sources

- [x] 2.1 Copy `main/pins.hpp` verbatim into harness `main/`
- [x] 2.2 Copy `components/monitor/include/calibration.hpp` verbatim into harness `main/`
- [x] 2.3 Copy the I2C init block from node `main/main.cpp` (`InitImuI2c`, `ImuReadReg`, `ImuWriteReg`, `MapImuOdr`, address/timeout constants) into harness `main.cpp`
- [x] 2.4 Record the node source commit hash + copied function line numbers in a harness `README.md` drift-tracking header (mirror `docs/natural-frequency-pipeline.md` "Last verified" convention)

## 3. Extract FFT analyzer module

- [x] 3.1 Create `main/fft_analyzer.hpp` with verbatim copies of `kTwoPi`, `kFftWindowSamples`, `FftBinRange`, `DominantAxis`, `SwayAxisResult`
- [x] 3.2 Port `SelectFftBinRange` to `fft_analyzer.cpp`: take `fmin,fmax` as params instead of `config_` members; logic identical
- [x] 3.3 Port `ComputeDominantAxisSway` to `fft_analyzer.cpp`: take `gx,gy,gz` linear arrays + `start,end,fs` params; replace `PhysicalIndex(start+i)` with `gx[start+i]`; logic identical
- [x] 3.4 Port `ComputeSignedAxisNaturalFrequency` → `ComputeNaturalFrequency` in `fft_analyzer.cpp`: take `axis, gx,gy,gz, start,count, fmin,fmax, fs, float* workspace`; replace ring indexing with linear; drop `ClearDashboardPsd`/`PublishDashboardPsdFromFft`; algorithm body (guards, Hann, mean-center, zero-pad, `dsps_fft2r_fc32`+`dsps_bit_rev_fc32`, in-band power argmax, bin→Hz, `max_bin==0→0`) identical
- [x] 3.5 Diff-port check: compare each ported function body line-by-line against node `modal_analyzer.cpp:60/264/319` to confirm only indexing/config/side-effect adaptations differ

## 4. Harness main + sampler task

- [x] 4.1 In `main.cpp` `app_main`: init I2C, init NVS, read calibration biases (fallback to defaults `gx=1.096412, gy=-2.593744, gz=0.414028`), construct + init `Lsm6ds3` with `ODR_26Hz` + read/write cbs, `dsps_fft2r_init_fc32(nullptr, 1024)`
- [x] 4.2 Add `CONFIG_FFT_VERIFY_MOUNT_LID` Kconfig option mirroring node's `CONFIG_MONITOR_IMU_MOUNT_LID` default
- [x] 4.3 Define static window buffers `float gx[130], gy[130], gz[130]` and static FFT workspace `std::array<float, 2*kFftWindowSamples>`
- [x] 4.4 Implement sampler task: `vTaskDelayUntil` at `ceil(1000/26)` ms, core 1, prio 5; read gyro, apply calibration (bias subtraction + lid-mount x/z sign-flip under config), store into window; on read failure `ESP_LOGW` + skip (no index advance)
- [x] 4.5 On 130th sample: call `ComputeDominantAxisSway` then `ComputeNaturalFrequency`; `ESP_LOGI` `fn_hz`, dominant axis name, `(sway_pp_x, sway_pp_y, sway_pp_z)`; reset window counter
- [x] 4.6 `xTaskCreatePinnedToCore` the sampler task from `app_main`; delete `app_main` after start

## 5. Build, flash, verify

- [x] 5.1 `idf.py set-target esp32s3` then `idf.py build` — confirm clean build, no node-repo references
- [x] 5.2 `idf.py -p <port> flash monitor` — confirm boot logs (I2C init, IMU init, FFT init) and 5 s window result lines appear
- [x] 5.3 Live verification: drive node at a known imposed oscillation frequency; confirm printed `fn_hz` matches within ~0.2 Hz (Rayleigh resolution at 5 s window); confirm dominant axis matches the driven axis
- [x] 5.4 Live verification: drive at a second distinct frequency (>0.2 Hz from the first); confirm `fn_hz` tracks the change across windows
