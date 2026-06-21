# Techniques & Methodologies

> **Last verified:** 2026-06-21 against `openspec/specs/` and `ARCHITECTURE.md` @ HEAD
>
> This document explains the **techniques and methodologies** applied by the ranting-jatuh node, and the reasoning behind each selection. It is deliberately implementation-agnostic: it names no firmware framework, programming language, memory layout, buffer sizes, or runtime/task primitives. For the implementation trace, follow the cross-references in each section.
>
> **Maintenance note:** Review this document whenever a technique is added, removed, or swapped. The specs and `ARCHITECTURE.md` remain the source of truth for behavior and structure; this document owns the *selection rationale* layer.

## Purpose & Scope

The node applies a chain of signal-processing, state-machine, reliability, and power-management techniques to infer tree-branch structural health from inertial and acoustic-emission data. Each technique was chosen over alternatives for a concrete reason — latency, power, robustness, data integrity, or determinism. That reasoning is scattered across capability specs, architecture notes, and inline code comments, and is not independently readable.

This document consolidates the **what / why-selected / alternative-rejected / what-it-achieves** for every technique, independent of any specific firmware framework, language, memory model, or runtime. It targets three audiences:

- **Structural-health and signal-processing reviewers** who can evaluate the methodology without reading embedded code.
- **New contributors** who need a map of the techniques before touching implementation.
- **Maintainers** evaluating whether a technique should be swapped out.

**How this differs from neighboring documents:**

| Document | Owns |
|----------|------|
| `openspec/specs/*` | Normative **requirements** (what the system SHALL do) |
| `ARCHITECTURE.md` | **Implementation structure** (file map, wiring, runtime layout) |
| `docs/natural-frequency-pipeline.md` | **Algorithmic trace** of one pipeline (FFT + damping) |
| `mqtt_interface.md` | **Payload schema** for the MQTT interface |
| **This document** | **Technique selection rationale** (why each method was chosen) |

## Cross-Reference Index

| Theme | Related specs | Related docs |
|-------|---------------|--------------|
| Sensor fusion | `adaptive-complementary-filter`, `imu-mount-transform` | `ARCHITECTURE.md` §5 |
| Disturbance detection | `imu-event-analysis`, `accel-error-state-detection`, `node-state-machine` | `docs/natural-frequency-pipeline.md` |
| Event state machine | `node-state-machine`, `free-decay-analysis` | `ARCHITECTURE.md` §5–§6 |
| Modal analysis | `free-decay-analysis`, `imu-event-analysis`, `envelope-damping-regression`, `noise-gate` | `docs/natural-frequency-pipeline.md` |
| Failure detection | `ae-spectral-detector`, `free-fall-debounce` | `ARCHITECTURE.md` §5 step 9 |
| Reliability & data integrity | `sd-upload-queue`, `imu-calibration`, `startup-time-sync`, `node-id-topic-prefix`, `monitor-mutex-safety`, `embedded-runtime-safety` | `ARCHITECTURE.md` §4, §14 |
| Power & network | `network-strategy`, `network-task`, `sd-upload-queue` | `mqtt_interface.md`, `ARCHITECTURE.md` §4 |
| Validation | `notebook-centerline-modal-analysis`, `embedded-runtime-safety` | `imu_algorithms/` |
| Engineering invariants | `embedded-runtime-safety`, `monitor-mutex-safety`, `firmware-structure` | `ARCHITECTURE.md` §14 |

---

## 1. Sensor Fusion & Orientation

### 1.1 Adaptive Complementary Filter

**What.** A complementary filter fuses gyroscope integration (high-pass, drifts over time) with an accelerometer gravity reference (low-pass, noisy during motion) through a blend coefficient `alpha`. The node uses an **adaptive** variant: `alpha` is recomputed per sample from the accelerometer magnitude error.

**Why selected.** Roll and pitch are needed for tilt monitoring and as a precondition for disturbance detection. A complementary filter is computationally cheap, has constant per-sample work, and is unconditionally stable in the sense that it cannot diverge the way an unbounded integrator can. The adaptive blend adds robustness under dynamic acceleration without changing the cost.

**Alternatives rejected.** The filter library ships three other orientation estimators — **Madgwick**, **Kalman**, and an **Extended Kalman Filter (EKF)**. They are compiled but **not instantiated**:

- **Madgwick** — gradient-descent quaternion estimator. Rejected as the primary filter because its tuning parameter is sensitive to motion regime and it offers no advantage for a branch-monitoring application dominated by slow tilt plus occasional transient disturbance.
- **Kalman** — linear quadratic estimator. Rejected because it assumes Gaussian noise and a linear model; orientation on a rotating branch is nonlinear, so a linear Kalman is a poor fit without an EKF.
- **EKF** — nonlinear extension. Rejected as the primary filter because its per-sample cost and tuning complexity are not justified when the adaptive complementary filter already handles the dynamic-acceleration case via alpha adaptation. The EKF remains available for future use.

**What it achieves.** Stable roll/pitch estimates that trust the gyroscope during disturbance and correct toward gravity when the branch is near-static, at constant per-sample cost.

### 1.2 Self-Tuning Alpha

**What.** The blend coefficient is computed as `alpha = 1 - (1 - alpha_base) * weight`, where `weight = 1 / (1 + K * |accel_magnitude - 1g|)`. Near 1 g, `alpha` approaches `alpha_base` (trust the accelerometer correction). Far from 1 g, `alpha` approaches 1 (trust the gyroscope entirely).

**Why selected.** A fixed `alpha` is a compromise: high alpha reduces accelerometer noise injection during motion but allows drift to accumulate when static; low alpha corrects drift quickly but is corrupted by any dynamic acceleration. Adapting alpha to the accelerometer magnitude error resolves the trade-off per sample.

**Alternative rejected.** A fixed alpha tuned for the static case. Rejected because branch disturbance events produce large transient accelerations that would corrupt the gravity reference; a fixed alpha would either over-trust the accelerometer during events or under-correct drift at rest.

**What it achieves.** Drift correction when the branch is still, and immunity to accelerometer corruption when the branch is moving — without a mode switch or event classifier.

### 1.3 Mount-Orientation Transform

**What.** A compile-time selectable axis transform (negate X and Z) converts sensor-frame data to branch-frame data when the PCB is mounted in the lid orientation (180° Y rotation). The transform is applied after bias subtraction and before any downstream consumer.

**Why selected.** The same firmware must run on boards mounted in either body or lid orientation. A compile-time transform keeps downstream code (filter, detector, analyzer) written once, in branch-frame coordinates.

**Alternative rejected.** Per-board firmware forks, or a runtime orientation flag. Rejected because a fork doubles maintenance, and a runtime flag adds a per-sample branch for no benefit when the orientation is fixed at assembly.

**What it achieves.** Single-source algorithms that are mount-agnostic; zero runtime overhead when the transform is disabled.

---

## 2. Disturbance Detection

### 2.1 Teager-Kaiser Energy Operator (TKEO)

**What.** A three-sample sliding-window energy operator over the gyro magnitude: `psi[n] = x[n-1]^2 - x[n-2] * x[n]`. TKEO estimates the instantaneous energy of a signal at each sample using only three adjacent values.

**Why selected.** A branch disturbance is a transient burst of rotational energy. A raw magnitude threshold sees the *amplitude* of the burst but is insensitive to its *rate of change*, so a slow drift can cross the same threshold as a sharp impact. TKEO emphasizes the product of amplitude and frequency, making transient bursts stand out from quasi-static motion. The three-sample window adds only one sample of latency and constant work.

**Alternative rejected.** A raw gyro-magnitude threshold alone. Rejected because it cannot distinguish a true disturbance from a slow posture change that happens to cross the magnitude threshold. (The system retains a magnitude threshold as a *secondary* onset trigger, but TKEO carries the primary disturbance signal — see §2.3.)

**What it achieves.** Transient-energy-sensitive disturbance detection with one-sample latency and constant per-sample cost.

### 2.2 High-Pass Filtering Before Thresholding

**What.** The disturbance threshold path applies a per-axis **Chebyshev Type I high-pass filter** to the calibrated inertial signal before threshold comparison, then uses a single absolute threshold on the high-passed magnitude.

**Why selected.** The inertial signal carries low-frequency content that is not disturbance: gravity, slow tilt, thermal drift, and the branch's quasi-static posture. If a threshold sees this low-frequency content, the detector must either set the threshold high enough to ignore it (missing small disturbances) or accept frequent false entry from drift. High-pass filtering first removes the drift/gravity baseline so the threshold sees only transient energy, allowing a single, well-calibrated absolute threshold.

**Alternative rejected.** The earlier `|accel_magnitude - 1g|` scalar metric with a two-threshold scheme (a separate enter/exit threshold on the scalar deviation). Rejected because a scalar deviation collapses three axes into one number, losing directional information, and its two-threshold structure was a workaround for the absence of proper baseline rejection. The per-axis Chebyshev HPF with a single absolute threshold replaced it: better baseline rejection, simpler threshold structure.

**What it achieves.** A single disturbance threshold that is robust to slow drift and gravity, without a two-threshold workaround.

### 2.3 Schmitt-Trigger State Detector with Hysteresis and Quiet Debounce

**What.** The detector uses separate **enter** thresholds (TKEO high, gyro-magnitude onset) and **exit** thresholds (TKEO low, gyro-magnitude quiet), with a configurable **quiet debounce** requiring N consecutive quiet samples before declaring the disturbance over.

**Why selected.** A single threshold produces **chatter**: as the signal hovers near the threshold, the detector flips in and out of the disturbed state on noise. Hysteresis (enter threshold above exit threshold) creates a dead band where small fluctuations cannot retrigger a transition. The quiet debounce adds a temporal filter — a momentary dip below the quiet threshold does not end the event; only a sustained quiet period does.

**Alternatives rejected.**
- **Single-threshold detector.** Rejected for the chatter reason above.
- **Continuous disturbance score** (e.g., a probability or confidence in `[0,1]`). Rejected because the downstream pipeline (buffer management, post-hoc analysis) needs a discrete event boundary to know *when* to analyze; a continuous score would require an additional thresholding step to produce that boundary, reintroducing the same problem.

**What it achieves.** A clean, discrete event boundary that is immune to boundary chatter and momentary quiet dips.

---

## 3. Event State Machine

### 3.1 Two-State IDLE/DISTURBED Model

**What.** The node operates in exactly two states: `IDLE` (branch at rest or under quasi-static load) and `DISTURBED` (a disturbance event in progress). Transitions are driven by the Schmitt-trigger detector from §2.3.

**Why selected.** The node's job is to detect *events* and analyze them post-hoc. The simplest model that captures this is binary: either an event is in progress or it is not. A two-state model gives an unambiguous event boundary (IDLE→DISTURBED is the onset; DISTURBED→IDLE is the offset) that the buffer management and post-hoc analysis can key on.

**Alternative rejected.** A richer state graph with an intermediate free-decay state (used in an earlier design). Rejected because the post-hoc analysis already identifies the decay region *within* the DISTURBED buffer retroactively (§4.1); an explicit runtime free-decay state duplicated that responsibility and added transition complexity without new information.

**What it achieves.** An unambiguous event boundary for buffer management and analysis, with no redundant runtime states.

### 3.2 Post-Hoc Analysis on the DISTURBED→IDLE Transition

**What.** Modal analysis (natural frequency, damping) runs **once**, retroactively, when the DISTURBED→IDLE transition fires — not continuously during the disturbance.

**Why selected.** Modal analysis needs the *full event window* to locate the decay onset, select a dominant axis, and fit a damping envelope. Running it mid-event would produce partial results on an incomplete window, then have to redo the work when the event ends. Running it once, after the event, gives a single result computed on the complete data.

**Alternative rejected.** Streaming analysis during the disturbance. Rejected because (a) partial mid-event results have no clear consumer — the warning is about the *event*, not a sample within it — and (b) recomputing at the end wastes the per-sample budget that real-time sampling cannot spare.

**What it achieves.** One complete-result analysis per event, with the per-sample sampling path kept free of analysis work.

### 3.3 Buffer Refresh on Prolonged Disturbance

**What.** If a disturbance runs long enough to fill the event buffer, the node publishes intermediate sway statistics, reports frequency and damping as zero for that intermediate payload, resets the event buffer with recent pre-trigger samples, and continues accumulating in DISTURBED.

**Why selected.** A single fixed-capacity buffer cannot hold an arbitrarily long disturbance. Rather than overflow and lose data, the node emits a sway-only intermediate result and restarts the buffer, preserving the ability to capture the *next* segment. Frequency and damping are reported zero for the refresh payload because a partial segment is not a complete decay region.

**Alternative rejected.** Unbounded buffer growth, or dropping the overflow. Rejected because unbounded growth violates the fixed-capacity discipline (§10.2), and dropping silently loses data.

**What it achieves.** Bounded memory under arbitrarily long disturbances, with intermediate sway data preserved.

---

## 4. Modal Analysis

Modal analysis runs on the DISTURBED→IDLE transition only. The full algorithmic trace (data flow, function calls, FFT parameters) lives in `docs/natural-frequency-pipeline.md`; this section documents the *technique selection* for each stage.

### 4.1 Decay-Onset Detection (TKEO + Peak Snap + Amplitude-Drop Validation)

**What.** Within the event buffer, a non-negative TKEO energy curve is computed over the gyro magnitude. An energy threshold (the larger of a noise floor and a fraction of the peak energy) selects candidate onset samples. The chosen onset is snapped to the nearest local gyro-magnitude peak within a small window. The candidate is validated by requiring a minimum number of samples after the onset and a minimum amplitude drop from peak to tail. The output is an onset index and a quality label (Reliable / Low / None).

**Why selected.** Damping regression assumes free decay — the part of the signal *after* the disturbance input stops. If the regression includes the forced portion (while the branch is still being driven), the fit is meaningless. Localizing the decay onset isolates the free-decay region. The snap-to-peak avoids starting mid-cycle (which would bias the envelope). The amplitude-drop validation rejects events that never actually decayed (e.g., a sustained offset).

**Alternative rejected.** Regressing over the entire DISTURBED buffer. Rejected because the forced portion contaminates the decay estimate.

**What it achieves.** A validated free-decay region for the downstream FFT and damping stages, with a self-assessed quality label.

### 4.2 Dominant-Axis Selection via Integrated Sway

**What.** Each calibrated gyro axis is integrated over the event segment to get cumulative angle. The peak-to-peak displacement (max minus min of the cumulative sum) is computed per axis. The axis with the largest peak-to-peak is the dominant axis; its signed data feeds the FFT.

**Why selected.** A branch oscillates primarily in one mode — one axis carries most of the modal energy. Analyzing the dominant axis gives the cleanest spectral estimate. Integrating to cumulative angle and taking peak-to-peak measures the *total angular travel* per axis, which is the right notion of "which axis moved the most" for a sway event.

**Alternative rejected.** Analyzing all three axes independently and reporting three frequencies. Rejected because (a) the off-axis energy is typically noise-dominated and produces a worse estimate, and (b) the downstream payload historically carries a single dominant-axis result, so reporting three would break schema compatibility without adding signal.

**Known limitation.** For a perfectly symmetric oscillation, the cumulative sum is near-zero (positive and negative halves cancel), so the peak-to-peak can understate a symmetric axis and select the wrong dominant axis. This is a known trade-off; the integrated-sway method was chosen because it is robust to the common case (asymmetric, dominant-axis oscillation) and the symmetric case is rare for branch events.

**What it achieves.** A single best-axis signal for spectral analysis, with a known failure mode documented for future improvement.

### 4.3 Natural-Frequency Estimation via FFT

**What.** The signed dominant-axis gyro decay segment is mean-centered, windowed with a **Hann window**, zero-padded to a power of two, and transformed with a radix-2 FFT. The power spectrum is scanned only within a **bounded search band** (default 0.5–12 Hz); the bin with the maximum power in that band maps to the natural frequency.

**Why selected (FFT).** The natural frequency is a spectral quantity; an FFT is the direct way to estimate it from a short segment. Alternatives like zero-crossing or peak-to-peak spacing are cheaper but assume a single clean harmonic and a zero-mean signal — fragile for a damped, noisy decay. The FFT is robust to noise and to the amplitude decay within the window.

**Why selected (Hann window).** A rectangular window produces spectral leakage (the decay segment is not periodic), smearing energy across bins. The Hann window tapers the ends to reduce leakage at the cost of slightly wider bins — the right trade-off for a short, decaying segment.

**Why selected (bounded search band).** Branch modal frequencies occupy a known low-frequency range. Scanning only that band rejects out-of-band peaks from noise, sensor artifacts, or higher harmonics that are not the fundamental mode. A bounded scan is also faster and cheaper than a full-spectrum search.

**Alternative rejected.** Zero-crossing and peak-to-peak spacing estimators. These exist in the Python reference implementation (`imu_algorithms/_extraction.py`) as alternative methods but were **not** ported to the node. Rejected for the node because they require a clean, single-harmonic, zero-mean signal and are fragile under noise; the FFT is the node's primary estimator.

**What it achieves.** A noise-robust natural-frequency estimate constrained to the physically meaningful band. See `docs/natural-frequency-pipeline.md` for the full FFT parameter trace (window size, bin resolution, search-band mapping).

### 4.4 Damping-Ratio Estimation via Peak-Hold Envelope + OLS Log-Linear Regression

**What.** An **asymmetric peak-hold envelope** is computed over the gyro magnitude: `env[n] = max(gmag[n], alpha * env[n-1])`, where `alpha` is a per-sample decay factor derived from a chosen corner frequency. The first cycle is skipped (it is dominated by the disturbance input, not free decay). The envelope is lower-bounded to reject near-zero trailing samples. An **ordinary least-squares (OLS) regression** of `ln(envelope)` versus time is then fit; the damping ratio is `zeta = |slope| / (2 * pi * f_natural)`.

**Why selected (peak-hold envelope).** Free decay follows `A(t) = A0 * exp(-zeta * omega_n * t)`, so `ln|A|` is linear in time with slope `-zeta * omega_n`. To get there, we need the decay *envelope* (the peak amplitudes), not the raw oscillating signal. A peak-hold envelope is a cheap, robust way to track decaying peaks without peak-detection logic that would need tuning (minimum spacing, prominence, etc.). The asymmetric form (peak-hold with exponential release) tracks the true upper envelope even when a local peak is slightly below the running envelope.

**Why selected (OLS log-linear regression).** Once the envelope is logarithmically linear, OLS is the maximum-likelihood estimator of the slope under Gaussian noise. It uses all envelope samples (not just detected peaks), giving a more stable estimate than a two-point slope from the first and last peak.

**Why selected (zeta formula).** From the standard second-order free-decay model, `ln|A| = ln(A0) - zeta * omega_n * t`, so `slope = -zeta * omega_n`, and `omega_n = 2 * pi * f_natural`. Therefore `zeta = |slope| / (2 * pi * f_natural)`. This ties the damping estimate to the FFT-derived natural frequency from §4.3.

**Alternative rejected.** A per-peak log-decrement (computing `zeta` from the log ratio of two successive peak amplitudes). Rejected because two-peak estimates are noise-sensitive; OLS over the full envelope is far more stable.

**What it achieves.** A damping-ratio estimate grounded in the standard free-decay model, stable under noise, with no peak-detection tuning.

### 4.5 Noise Gate as an Independent Damping Gate

**What.** A separate **noise gate** on the peak gyro magnitude decides whether damping is computed at all. If the peak magnitude in the event segment is below a configurable threshold, damping is reported as zero with "low" confidence, **but the natural frequency is still computed and published normally**.

**Why selected.** Damping regression on a noise-level signal produces a meaningless number that *looks* like a valid damping ratio. The noise gate refuses to compute damping when there is no signal above the noise floor, preventing spurious damping values from reaching consumers. Crucially, the gate is **independent** of the frequency path: a low-amplitude event can still have a well-estimated frequency (the FFT is robust to amplitude), so frequency is published regardless.

**Alternative rejected.** A single combined quality gate that suppresses both frequency and damping when amplitude is low. Rejected because frequency and damping have different noise sensitivities — frequency survives at low amplitude, damping does not — so gating them together would discard valid frequency data.

**What it achieves.** Spurious-damping prevention without throwing away valid frequency estimates on low-amplitude events.

### 4.6 Confidence Tiers (Self-Assessment)

**What.** Every damping result carries a confidence label — **high**, **medium**, or **low** — derived from a combination of gates: the regression's R², the number of cycles in the decay region, the amplitude drop from start to end, the samples-per-cycle, and the decay-onset quality label from §4.1.

**Why selected.** A single damping number with no quality signal is dangerous: a consumer cannot tell a high-quality fit from a gate-failed zero. The tiers give consumers a tractable way to weight results: trust "high", down-weight "medium", discard "low". The gates are chosen so that "high" requires strong evidence (high R², enough cycles, sufficient amplitude drop, enough samples per cycle, reliable onset), while "low" captures everything that failed any gate.

**Alternative rejected.** Publishing the raw R² and letting each consumer threshold it. Rejected because (a) R² alone is insufficient (a high R² on too few cycles is still unreliable), and (b) every consumer would re-implement the thresholding logic. The tiers consolidate the multi-gate assessment once.

**What it achieves.** A consumer-facing quality signal that encodes a multi-factor reliability assessment in a single label.

---

## 5. Failure Event Detection

Failure events are detected on **parallel hardware paths** distinct from the modal-analysis pipeline. They are not derived from the disturbance detector; they are independent failure channels.

### 5.1 Free-Fall Detection via IMU Hardware Interrupt

**What.** Free-fall is detected by the IMU's on-chip hardware interrupt (a dedicated free-fall recognition block), which raises an interrupt when the accelerometer sees near-zero gravity for a configured duration. The node reads the interrupt status flag and publishes a free-fall failure event.

**Why selected.** Free-fall is a physics-level condition (all axes near zero simultaneously) that the IMU's dedicated hardware can recognize with lower latency and lower power than software polling of the raw accelerometer stream. Offloading the detection to the IMU's hardware block means the node does not spend per-sample work looking for free-fall.

**Alternative rejected.** Software polling of the accelerometer magnitude in the sampling loop. Rejected because it would add per-sample work for a rare event, and would be slower and less power-efficient than the hardware block.

**What it achieves.** Low-latency, low-power free-fall detection with zero per-sample software cost.

### 5.2 Acoustic-Emission Detection across Three Modes

**What.** Acoustic emission (AE) — high-frequency stress waves that precede or accompany crack growth and branch failure — is detected in one of three selectable modes:

1. **GPIO digital interrupt** — a digital AE sensor output drives a GPIO interrupt on a rising edge.
2. **ADC threshold** — an analog AE signal is sampled, and a threshold crossing raises a failure.
3. **Spectral / FFT** — a continuous ADC stream is windowed, FFT-transformed, and high-frequency bin energy feeds an adaptive-gradient danger detector with an energy-jump latch.

**Why AE complements IMU-based detection.** The IMU detects *motion* (a fall, a sway). Acoustic emission detects *material failure* (crack growth, fiber fracture) that may precede any measurable motion. The two channels are independent evidence of branch failure: AE can fire before the branch moves, and free-fall fires when it drops.

**Why multiple AE modalities exist.** Different AE sensors produce different outputs (digital pulse vs. analog waveform). The digital GPIO mode is the cheapest and works with threshold-output sensors. The ADC threshold mode handles analog sensors with a simple level detector. The spectral mode is for analog sensors where frequency-domain discrimination is needed to separate crack-related emissions from background noise — it uses a Hamming-windowed FFT over a configured bin range, an EWMA baseline (mean and variance) for the gradient, and a dynamic danger threshold (`mu + K * sigma`), with an energy-jump latch gating publication to avoid floods. The three modes let the same firmware support different sensor deployments.

**Alternative rejected.** A single AE mode. Rejected because it would lock the hardware to one sensor type; field deployments differ.

**What it achieves.** Material-failure detection that is independent of motion-based detection and adaptable to the deployed AE sensor type.

### 5.3 Free-Fall Debounce (Publish Cooldown)

**What.** After a free-fall failure event is published, subsequent free-fall interrupts within a configurable cooldown window are suppressed. The cooldown defaults to 10 seconds. Other failure types (acoustic emission) are unaffected.

**Why selected.** A single physical fall produces multiple hardware interrupts as the branch tumbles and the accelerometer re-enters the free-fall condition. Without a cooldown, one fall would publish a burst of duplicate failure events, flooding the outbox and the MQTT topic. The cooldown collapses a burst into one published event.

**Alternative rejected.** Publishing every interrupt. Rejected because the duplicates carry no new information and waste power (each publish wakes the network path in on-demand mode).

**What it achieves.** One published failure per physical fall event, with the network path woken once.

---

## 6. Reliability & Data Integrity

### 6.1 Storage-First Outbox

**What.** Every payload (parameter JSON, failure record) is written to the SD card `outbox/pending/` directory **before** any network publish is attempted. On a successful publish, the file is moved to `outbox/sent/`; old sent files are pruned to a retention limit. On reboot, any files left in `pending/` are uploaded before newly-created files.

**Why selected.** The node may lose power, lose WiFi, or reboot at any time. If a payload existed only in memory or in a network send buffer, it would be lost. Persisting to SD first guarantees durability: the data survives anything short of SD-card failure. The outbox pattern (pending → sent) gives a clean retry boundary — a file stays pending until it is confirmed published, then moves to sent for retention and later pruning.

**Alternative rejected.** Publish-then-persist, or memory-only queuing. Rejected because either loses data on power loss or network outage.

**Trade-off.** Every payload is written to SD twice in the common case (once to pending, once moved to sent). This doubles SD I/O per payload. Accepted because durability is a hard requirement and SD writes are cheap relative to the value of not losing failure events.

**What it achieves.** Data durability across power loss, WiFi outage, broker restart, and node reboot.

### 6.2 Publish/Subscribe Decoupling

**What.** The monitor (data producer) publishes events to a **publish/subscribe event bus** without knowing who, if anyone, is subscribed. The logger and the dashboard register as subscribers independently. The monitor's code contains no reference to the logger or dashboard.

**Why selected.** Decoupling the producer from the consumers means a new consumer (e.g., a future aggregator or a debug tap) can be added by registering a new subscriber, with zero changes to the monitor. It also means a slow consumer cannot block the monitor's sampling path — the event is dispatched to the consumer's own execution context.

**Alternative rejected.** Direct calls from monitor to logger (and dashboard). Rejected because it would couple the monitor to every consumer, block the sampling path on consumer speed, and require the monitor to change whenever a consumer is added or removed.

**What it achieves.** A monitor that is consumer-agnostic and a sampling path that is immune to consumer slowness.

### 6.3 Calibration Bias Subtraction and Tilt Taring

**What.** Static accelerometer and gyroscope biases are stored in persistent non-volatile storage and subtracted from every raw sample before it enters the orientation filter. The biases are read once at startup and applied per sample. A separate taring step removes the baseline tilt so that the reported roll/pitch is relative to the branch's installed posture, not to absolute horizontal.

**Why selected.** Every MEMS IMU has a static bias (zero-rate offset for the gyro, gravity-axis offset for the accelerometer) that, if uncorrected, integrates into drift in the orientation estimate. Bias subtraction is the standard precondition for any orientation filter. Taring to the installed posture is necessary because a branch is not installed horizontal; reporting tilt relative to horizontal would show a constant offset that masks the actual sag/twist signal.

**Why this is a methodology step, not a one-off.** Biases can drift over temperature and time; taring must be re-done if the node is re-mounted. Treating calibration as a persistent, read-at-startup methodology step (rather than a factory one-off) means the node can be re-calibrated in the field and the correction persists across reboots.

**Alternative rejected.** No bias subtraction (accept the drift). Rejected because the drift would corrupt tilt monitoring and the disturbance detector's baseline within minutes.

**What it achieves.** Drift-free orientation estimates and tilt readings that are referenced to the branch's installed posture.

### 6.4 Startup NTP Time Synchronization

**What.** During boot, after the network interface is initialized, the node performs an SNTP time synchronization against a configured NTP server. All subsequent log entries and data timestamps use the synchronized clock. If sync fails at boot (network unavailable), the node continues startup with an unsynchronized clock and retries sync during later network cycles.

**Why selected.** Every published record carries a timestamp, and the server correlates records from many nodes across time. Without a synchronized clock, timestamps are meaningless for cross-node correlation and for relating a failure event to external conditions (wind gusts, weather). Performing sync at boot means the first published records are already correctly timestamped; retrying later covers the case where the network was unavailable at boot.

**Alternative rejected.** Relying on the node's monotonic uptime for timestamps. Rejected because uptime is node-local and cannot be correlated across nodes or with server-side events.

**What it achieves.** Timestamps that are comparable across nodes and correlatable with external time references.

### 6.5 Stable Node Identity

**What.** Each node has a persistent, human-readable identifier (`adjective-noun`, e.g., `quiet-pine`) stored in non-volatile storage and used as the prefix in every MQTT topic (`ranting/{node_id}/parameters`, `ranting/{node_id}/failures`, `ranting/{node_id}/verify`). On first boot, an ID is generated randomly from word pools and persisted. A build-time override can fix the ID for factory provisioning.

**Why selected.** The server must distinguish records from different nodes. A persistent, human-readable ID is better than a MAC address (not human-readable) or a runtime-random ID (changes every boot, breaking traceability). Generating on first boot and persisting means the ID is stable across reboots without requiring per-node provisioning; the build-time override handles the factory-provisioning case where a known ID is required.

**Alternative rejected.** Using the network MAC address as the node ID. Rejected because it is not human-readable in logs or dashboards, and it changes if the network hardware changes.

**What it achieves.** Stable, traceable, human-readable node identity for topic addressing and server-side record attribution.

---

## 7. Power & Network Strategy

### 7.1 On-Demand WiFi vs. Persistent WiFi

**What.** The node selects between two WiFi strategies at build time:

- **On-demand** (field deployment): for each publish cycle, connect WiFi, publish, then disconnect and power down the radio. Between cycles, WiFi is off.
- **Persistent** (dashboard/debug): keep WiFi connected and auto-reconnect on disconnect.

**Why selected (on-demand).** WiFi is the largest single power draw on the node. In field deployment on a battery-powered branch node, the radio should be on only for the seconds needed to publish, not the hours between publishes. Connect-publish-disconnect minimizes radio-on time and extends battery life.

**Why selected (persistent).** In dashboard/debug mode, the node serves a live HTTP dashboard that must be reachable at any time; disconnecting WiFi between publishes would break the dashboard. Auto-reconnect handles transient WiFi drops without operator intervention.

**Alternative rejected.** A single strategy. Rejected because the field and dashboard use cases have opposite power-vs-availability trade-offs; forcing one would either waste power in the field or break the dashboard.

**What it achieves.** Power-optimal radio behavior in the field and always-available connectivity for debugging, selected per build.

### 7.2 MQTT QoS 0 (At-Most-Once)

**What.** The node publishes MQTT payloads at QoS 0 — the broker does not acknowledge, and the node does not retry at the MQTT protocol level.

**Why selected.** The monitoring data model is a periodic stream of parameter records plus occasional failure records. A missed parameter record is acceptable because the next one will arrive; the value is in the trend, not any single record. QoS 0 avoids the per-message handshake overhead of QoS 1/2, which is significant at the publish rates and power budget of a field node. The outbox (§6.1) compensates for the lack of MQTT-level retry: every payload is on SD before publish, so a publish that does not reach the broker is still in `pending/` and will be re-sent on the next network cycle.

**Alternative rejected.** MQTT QoS 1 (at-least-once) or QoS 2 (exactly-once). Rejected because the handshake overhead costs power and latency, and the outbox already provides the durability that QoS 1/2 would buy. Failure records — which are less frequent and more important — are still carried by the same outbox durability, so they survive a missed publish even at QoS 0.

**What it achieves.** Low-overhead publishing with durability provided by the outbox rather than by the MQTT protocol.

### 7.3 Outbox Drain with Exponential Backoff

**What.** When a WiFi or MQTT connection attempt fails, the network path backs off exponentially: the first retry waits a base interval, each subsequent failure doubles the interval, up to a maximum (default cap: 1 hour). A successful connection resets the backoff to the base interval. No publish attempts are made while the backoff timer is running.

**Why selected.** A field node may be out of WiFi range for hours (e.g., a gust knocked the access point out). Retrying at a fixed short interval would burn power cycling the radio for no gain. Exponential backoff conserves power during long outages while still reconnecting promptly once the network returns. The cap prevents the backoff from growing so long that data delivery is delayed indefinitely after a transient outage.

**Alternative rejected.** Fixed-interval retry. Rejected because it wastes power during long outages and can retry-storm a recovering access point.

**Urgency override.** Failure files are urgent: if a failure file is pending and no backoff is active, the network path attempts a publish cycle without waiting for the parameter publish cadence. This ensures failure notifications are delivered promptly when the network is available, while parameter data respects its slower cadence.

**What it achieves.** Power-efficient resilience to long network outages, with prompt delivery of failure notifications when the network is available.

---

## 8. Validation Methodology

### 8.1 Python Reference Implementation Parity

**What.** A parallel offline pipeline (`imu_algorithms/`) implements the same algorithms — TKEO disturbance detection, decay-onset localization, dominant-axis selection, FFT natural frequency, peak-hold envelope damping — in Python, operating on recorded node data. The node's algorithms are validated against this reference.

**Why selected.** A Python reference is fast to iterate on, easy to visualize, and runs on recorded data without hardware. By keeping the reference and the node algorithms aligned, the node's results can be checked against the reference's results on the same input, catching porting bugs and regressions. The reference also serves as executable documentation of the intended algorithm behavior.

**Alternative rejected.** Validating the node algorithms only on-target against synthetic inputs. Rejected because on-target iteration is slow, visualization is limited, and a regression that produces a *plausible but wrong* number is hard to catch without a reference to compare against.

**What it achieves.** A fast, visualizable reference for every node algorithm, enabling regression detection and executable specification of intended behavior.

### 8.2 Self-Assessment Signals for Consumers

**What.** The node publishes self-assessment signals alongside its results: the **confidence tier** (§4.6) on every damping estimate, and the **noise gate** (§4.5) outcome (reflected as a "low" confidence with zero damping). Together these tell a consumer when to trust a result and when to down-weight or discard it.

**Why selected.** A monitoring system that publishes a number with no quality signal invites misuse: a consumer will treat every number as equally valid, and a gate-failed zero will be averaged into a trend as if it were a real measurement. By attaching a self-assessed confidence to every result, the node lets the consumer make an informed trust decision without re-implementing the gates.

**How a consumer should weight them.** Treat "high" confidence as a valid measurement; down-weight or flag "medium"; discard "low" (it means a gate failed or the signal was below the noise floor). A zero damping with "low" confidence is not a measurement of zero damping — it is the node saying "I could not measure damping here."

**Alternative rejected.** Publishing raw internal metrics (R², cycle count, amplitude drop) and letting the consumer decide. Rejected because every consumer would re-implement the trust logic, and the raw metrics are not independently interpretable without the gate definitions.

**What it achieves.** A consumer-actionable quality signal on every result, preventing gate-failed zeros from being treated as measurements.

---

## 9. Engineering Invariants

These are methodology-level disciplines, not implementation details. They describe *why* the node is structured the way it is, framed at the concept level. For the structural trace, see `ARCHITECTURE.md`.

### 9.1 Deterministic Sampling Path

**What.** The per-sample work — reading the IMU, applying calibration, updating the filter, running the disturbance detector, storing the sample — performs only bounded, fixed-cost operations. It performs no unbounded allocation, no file I/O, no network I/O, and no unbounded loops over external data.

**Why selected.** The sampling loop runs at a fixed rate (default 26 Hz) for the life of the node. Any per-sample work that can block or grow unbounded (a dynamic allocation that fragments, a file write that stalls, a network call that hangs) will eventually perturb the sampling timing and corrupt the time-series. Keeping the sample path bounded and I/O-free guarantees that the sampling period is deterministic.

**Alternative rejected.** Allowing the sample path to do "convenient" work (e.g., logging a debug line, allocating storage on the fly). Rejected because convenience work accumulates and eventually breaks timing; the discipline is cheaper to maintain than the bugs it prevents.

**What it achieves.** A deterministic sampling period, which is the precondition for every downstream signal-processing technique that assumes uniform sampling.

### 9.2 Fixed-Capacity Storage

**What.** Every buffer that holds event samples, history, or spectral data has a capacity fixed at design time. No buffer grows at runtime in response to data volume.

**Why selected.** A disturbance event can be arbitrarily long, and the spectral analysis needs fixed-size workspaces. If buffers grew dynamically, a long event or a pathological input could exhaust memory and crash the node. Fixed capacities make the memory budget a design-time decision, not a runtime gamble.

**Alternative rejected.** Dynamically-sized storage that grows with event length. Rejected because it violates the deterministic sampling path (§9.1) and makes the memory budget unpredictable.

**What it achieves.** A bounded, design-time memory budget that holds under any input.

### 9.3 Compile-Time Assertion of Structural Assumptions

**What.** Relationships between buffer sizes, alignment requirements, and power-of-two constraints (for the FFT) are checked at build time, not at runtime. A configuration that violates a relationship fails the build.

**Why selected.** The FFT requires power-of-two lengths; spectral workspaces must align with their inputs; sample-rate-derived constants must be consistent. Checking these at build time means a misconfiguration (e.g., a build-time config that sets the FFT size to a non-power-of-two) is caught before the node ever runs, rather than producing silent spectral garbage or a runtime crash on the target.

**Alternative rejected.** Runtime checks that validate these relationships on boot. Rejected because runtime checks on a deployed node either (a) fail and leave the node unusable with no easy fix, or (b) pass and then the misconfiguration still affects every subsequent computation. Build-time checks fail fast and fail early, at the developer's desk.

**What it achieves.** Structural misconfigurations caught at build time, never reaching a deployed node.

### 9.4 Scaled-Integer Configuration

**What.** Build-time configuration values that represent floating-point quantities (thresholds, frequencies, gains) are stored as scaled integers (e.g., a frequency stored as `value * 10`) and converted to floating point at runtime when read.

**Why selected.** Build configuration systems traditionally work in integers, not floating point. Storing a threshold as a scaled integer lets the configuration system represent it cleanly (e.g., "8.0 dps" becomes `80` with a ÷10 scale) while the runtime gets a proper floating-point value. It also avoids floating-point representation ambiguity in the configuration tooling.

**Alternative rejected.** Storing raw floating-point configuration values. Rejected because the configuration system does not natively support them, and ad-hoc float parsing in configuration is error-prone.

**What it achieves.** Clean, unambiguous representation of floating-point configuration in an integer-native configuration system.

### 9.5 Sampling-Isolation from I/O Jitter

**What.** The time-critical sampling and detection path is isolated, at the execution level, from the storage and network paths. Storage writes (SD) and network work (WiFi, MQTT) run in separate execution contexts from sampling, so their jitter (a slow SD write, a WiFi reconnect) cannot perturb the sampling period.

**Why selected.** SD writes and WiFi operations have highly variable latency (hundreds of milliseconds to seconds). If the sampling loop shared an execution context with them, a slow I/O operation would delay the next sample and break the uniform-sampling assumption that every downstream algorithm relies on. Isolating the contexts means I/O latency affects only the I/O path, not the sampling rate.

**Alternative rejected.** Running sampling and I/O in the same execution context with cooperative scheduling. Rejected because cooperative scheduling cannot bound the delay a slow I/O operation imposes on the next sample.

**What it achieves.** A sampling period that is immune to SD and WiFi latency, preserving the uniform-sampling assumption end-to-end.

---

## Maintenance

This document describes *why* each technique was selected. The *what* (requirements) lives in `openspec/specs/`; the *how* (structure) lives in `ARCHITECTURE.md`; the full FFT/damping trace lives in `docs/natural-frequency-pipeline.md`.

When a technique is added, removed, or swapped:

1. Update the relevant section here with the new selection rationale (what / why / alternative rejected / what it achieves).
2. Update the cross-reference index if the change touches a spec.
3. Update the "last verified" date.
4. If the change is an OpenSpec change, cross-reference this document in the change's proposal so the rationale is reviewed alongside the code.
