## Why

The current acoustic emission path only supports a digital GPIO interrupt or a low-rate ADC threshold check. The Arduino prototype adds a spectral high-frequency energy detector that should run on ESP-IDF and feed the existing failure event pipeline without adding a separate warning output.

## What Changes

- Add an ESP-IDF acoustic emission spectral ADC detector based on the Arduino prototype:
  - 40 kHz ADC sampling
  - 256-sample FFT window
  - Hamming window
  - high-frequency energy sum over FFT bins 64 through 127
  - crack latch from sudden energy increase
  - leaking integrator, gradient ring buffer, and adaptive EWMA threshold
- Treat both detected crack latch events and dynamic danger events as `FailureEvent::AcousticEmission`.
- Route all output through the existing monitor failure event, logger SD, and MQTT outbox pipeline.
- Discard the Arduino digital output/status pin behavior; no firmware GPIO status output is required.
- Keep existing GPIO interrupt and simple ADC threshold modes available unless replaced by configuration.

## Capabilities

### New Capabilities
- `ae-spectral-detector`: Acoustic emission spectral ADC detector behavior, configuration, and failure publication semantics.

### Modified Capabilities
- `embedded-runtime-safety`: Add bounded execution, stack/heap, and backpressure requirements for the high-rate ADC/FFT acoustic emission processing path.

## Impact

- Affected code:
  - `components/monitor/include/monitor.hpp`
  - `components/monitor/monitor.cpp`
  - `components/monitor/Kconfig`
  - `components/monitor/CMakeLists.txt`
  - `components/monitor/test/*`
  - `main/main.cpp`
- Existing logger failure formatting and MQTT/SD behavior should remain compatible because output stays `FailureEvent::AcousticEmission`.
- Dependencies should remain ESP-IDF ADC driver plus existing `esp-dsp`; no Arduino libraries are introduced.
