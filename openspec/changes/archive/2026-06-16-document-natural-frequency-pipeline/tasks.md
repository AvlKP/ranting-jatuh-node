## 1. Document Structure

- [x] 1.1 Create `docs/natural-frequency-pipeline.md` with top-level outline: FSM trigger, pipeline stages, FFT internals, damping, output fields, Python comparison
- [x] 1.2 Add "Overview" section with context paragraph and cross-reference links to existing specs (`free-decay-analysis`, `imu-event-analysis`, `envelope-damping-regression`, `noise-gate`)

## 2. Pipeline Diagrams

- [x] 2.1 Add mermaid `sequenceDiagram` showing DISTURBED→IDLE transition triggering `AnalyzeImuEvent()` → `FindDecayOnsetTkeo()` → `ComputeDominantAxisSway()` → `ComputeSignedAxisNaturalFrequency()` → `ComputePeakHoldDamping()`
- [x] 2.2 Add mermaid `flowchart LR` showing internal FFT pipeline: signed axis data → mean-center → Hann window → ESP-DSP radix-2 FFT → bit-reversal → bin power scan within search band → max bin to Hz
- [x] 2.3 Add mermaid `classDiagram` or table showing data flow through `MonitorResult` fields (`natural_freq_hz`, `natural_freq_roll_hz`, `natural_freq_pitch_hz`, `damping_confidence`)

## 3. FFT Parameter Reference

- [x] 3.1 Document FFT window size (`kFftWindowSamples = 1024`) and the dynamic 512/1024 selection logic
- [x] 3.2 Document bin resolution at default 26 Hz sample rate for both 512-pt and 1024-pt FFT
- [x] 3.3 Document configurable search band: `MONITOR_MODAL_FREQ_MIN_HZ_X10` (default 0.5 Hz) and `MONITOR_MODAL_FREQ_MAX_HZ_X10` (default 12.0 Hz) with bin mapping formula
- [x] 3.4 Add parameter reference table mapping Kconfig keys, C++ config fields, defaults, and algorithm impact

## 4. Damping Pipeline Documentation

- [x] 4.1 Document `ComputePeakHoldDamping()`: peak-hold envelope, OLS log-fit regression, zeta formula (`|slope| / (2 * pi * fn)`), confidence gating
- [x] 4.2 Document noise gate (`config_.noise_gate_gmag_dps`) and how it gates damping independently from frequency
- [x] 4.3 Document confidence tiers (high/medium/low) and their gate criteria (R-squared, cycles, amplitude drop, samples per cycle)

## 5. C++ vs Python Comparison

- [x] 5.1 Add comparison table: FFT type (ESP-DSP complex vs numpy rfft), window (Hann vs Hanning), padding (zero-pad to 512/1024 vs actual length), metric (power vs magnitude), search band (0.5-12 configurable vs 0.5-25 hardcoded)
- [x] 5.2 Document Python-only methods not in C++: `extract_frequency_zc()` (zero-crossing), `extract_frequency_pk()` (peak-to-peak), and why they exist only in reference

## 6. Cross-References and Polish

- [x] 6.1 Add line-number cross-references for key functions in `monitor.cpp`, `monitor.hpp`, `_extraction.py`
- [x] 6.2 Add "last verified" date and maintenance note
- [x] 6.3 Link `docs/natural-frequency-pipeline.md` from `ARCHITECTURE.md` line 219 table entry
