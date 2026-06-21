## 1. Document Scaffold & Scope

- [x] 1.1 Create `docs/techniques-and-methodologies.md` with top-level outline matching the themed sections from design.md (sensor fusion, disturbance detection, event state machine, modal analysis, failure detection, reliability & data integrity, power & network strategy, validation methodology, engineering invariants)
- [x] 1.2 Add a "Purpose & Scope" section stating the implementation-agnostic constraint (no framework, language, memory-layout, or task-runtime detail), the intended audience, and how this doc differs from specs and `ARCHITECTURE.md`
- [x] 1.3 Add a cross-reference index listing the related specs, `ARCHITECTURE.md`, `docs/natural-frequency-pipeline.md`, and `mqtt_interface.md`

## 2. Sensor Fusion & Orientation

- [x] 2.1 Document the adaptive complementary filter: what it is, why selected (cheap, stable, drift-corrected), and the alternatives rejected (Madgwick, Kalman, EKF — present in the filter library but not instantiated) with the reason each was rejected
- [x] 2.2 Document the self-tuning alpha rationale: why the blend coefficient adapts to accelerometer deviation from 1 g, and what it achieves under dynamic acceleration

## 3. Disturbance Detection

- [x] 3.1 Document the TKEO (Teager-Kaiser Energy Operator) technique: why an energy operator over a raw magnitude threshold, what it gains (transient energy emphasis, short window), and the trade-off
- [x] 3.2 Document the high-pass filtering role in the disturbance threshold path: why high-pass before thresholding
- [x] 3.3 Document the Schmitt-trigger state detector with hysteresis and quiet-debounce: why hysteresis + debounce prevents chatter at the boundary

## 4. Event State Machine

- [x] 4.1 Document the IDLE/DISTURBED state machine: why an explicit two-state model with hysteresis rather than continuous scoring
- [x] 4.2 Document the post-hoc analysis methodology: why modal analysis runs only on the DISTURBED→IDLE transition instead of streaming during the disturbance, and what that gains (full event window, no mid-event partial results)

## 5. Modal Analysis

- [x] 5.1 Document the decay-onset detection technique (TKEO over the event window + snap to nearest peak + amplitude-drop validation): why onset localization matters and why the validation gates exist
- [x] 5.2 Document dominant-axis selection via integrated sway (peak-to-peak of cumulative sum): why a dominant axis is chosen per event rather than analyzing all three, and the known symmetric-oscillation limitation
- [x] 5.3 Document natural-frequency estimation via FFT (Hann window, mean-centering, zero-pad, bounded search band, power-bin scan): why FFT, why Hann, why a search band, and link to `docs/natural-frequency-pipeline.md` for the full trace
- [x] 5.4 Document damping-ratio estimation via asymmetric peak-hold envelope + OLS log-linear regression: why peak-hold envelope, why log-linear regression, and the zeta formula rationale
- [x] 5.5 Document the noise gate as an independent damping gate (separate from frequency gating): why damping is gated independently from frequency
- [x] 5.6 Document the confidence-tier methodology (R², cycle count, amplitude drop, samples-per-cycle): why self-assessment tiers exist and how a consumer should weight them

## 6. Failure Event Detection

- [x] 6.1 Document free-fall detection via the IMU hardware interrupt path: why a hardware-detected failure path rather than software polling
- [x] 6.2 Document acoustic-emission detection across three modes (GPIO digital interrupt, ADC threshold, spectral/FFT): why AE complements IMU-based detection, and why multiple AE modalities exist
- [x] 6.3 Document the free-fall debounce rationale: why a single interrupt is not trusted as a confirmed failure

## 7. Reliability & Data Integrity

- [x] 7.1 Document the storage-first outbox methodology (persist every payload to SD before any network publish): why data survives reboot/outage, and the trade-off
- [x] 7.2 Document the publish/subscribe event-loop decoupling between producers and consumers: why the monitor is unaware of logger/dashboard, and what that gains (new subscribers without producer changes)
- [x] 7.3 Document calibration bias subtraction and tilt taring: why baseline removal is a methodology step rather than a one-off
- [x] 7.4 Document startup NTP time sync: why synchronized timestamps are a precondition for correlated server-side analysis
- [x] 7.5 Document stable node identity derivation: why a persistent node ID is required for topic addressing and traceability

## 8. Power & Network Strategy

- [x] 8.1 Document the on-demand WiFi strategy (connect, publish, disconnect) versus persistent WiFi: why on-demand for field deployment, and when persistent is preferred (dashboard/debug)
- [x] 8.2 Document the MQTT QoS 0 (at-most-once) choice: why at-most-once fits the monitoring model, and why the outbox compensates
- [x] 8.3 Document the outbox-drain with exponential backoff on failure: why resilient eventual delivery rather than retry-storm

## 9. Validation Methodology

- [x] 9.1 Document the Python reference implementation parity methodology: why a parallel offline pipeline exists, and how it is used to validate node algorithms
- [x] 9.2 Document how confidence tiers and noise gates serve as self-assessment signals that tell consumers when to trust a result

## 10. Engineering Invariants (Concept Level)

- [x] 10.1 Document the deterministic-sampling-path discipline: why the per-sample work is bounded (no unbounded allocation, no I/O in the sample path), framed as a real-time methodology rather than a memory detail
- [x] 10.2 Document the fixed-capacity-storage discipline: why all event/history buffers are bounded at design time
- [x] 10.3 Document the compile-time-assertion discipline: why size/alignment relationships are checked at build time rather than runtime
- [x] 10.4 Document the scaled-integer-configuration discipline: why configuration values use scaled-integer representation
- [x] 10.5 Document the sampling-isolation-from-I/O discipline: why the time-critical sampling loop is isolated from storage and network jitter, framed conceptually without naming runtime primitives

## 11. Cross-References & Polish

- [x] 11.1 Add a "last verified" date and a maintenance note tying the doc's review to any future technique change
- [x] 11.2 Link `docs/techniques-and-methodologies.md` from `README.md` (Documentation section) and from `ARCHITECTURE.md` (cross-reference area)
- [x] 11.3 Sweep every section to verify no implementation detail leaked (framework APIs, language constructs, memory sizes, task/priority/core runtime detail); remove or generalize any that did
