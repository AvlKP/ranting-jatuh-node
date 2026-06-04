# posthoc-decay-analysis Design

## Context

The current monitor FSM has three states: IDLE → DISTURBED → FREE_DECAY → IDLE. The DISTURBED→FREE_DECAY transition uses a 128-sample debounce on `accel_err_var < threshold_mid` (~4.9s at 26Hz). This debounce consumes the physical decay period: by the time FREE_DECAY is entered, the branch has already settled and `accel_err_var` is near-zero. FREE_DECAY immediately exits to IDLE, producing no meaningful natural frequency or damping results.

The damping algorithm uses a 3-peak log decrement (`kDecaySpan=3`), which is noisy for lightly-damped systems where many peaks are available. Peak detection parameters (`PEAK_MIN_SPACING=5`, `PEAK_MIN_AMPLITUDE=2°`) are too restrictive: spacing limits detection to ≤2.6Hz (misses 3–12Hz bench samples), and amplitude threshold misses late-decay peaks.

The storage buffer holds 5min × 60 × 26Hz = 7800 samples per axis. The decay data is already present in this buffer during the DISTURBED period — the problem is timing the transition to capture it, not storage capacity.

## Goals / Non-Goals

**Goals:**
- Reliably compute natural frequency and damping ratio from every disturbance event
- Eliminate the timing paradox where debounce consumes the decay window
- Support the full frequency range: 0.2–2Hz (field branches) and 3–12Hz (bench samples)
- Improve damping estimation robustness for lightly-damped systems
- Simplify FSM to two states (IDLE ↔ DISTURBED)

**Non-Goals:**
- Real-time decay tracking during the decay phase
- Changing the FFT algorithm (Welch / zero-padded short FFT stays as-is)
- Modifying the storage buffer size or structure
- Changing IDLE→DISTURBED entry logic
- Power optimization changes

## Decisions

### D1: Post-hoc retroactive analysis over real-time decay detection

**Decision:** When DISTURBED→IDLE transition fires, retroactively scan the stored buffer to identify the decay region and compute frequency/damping from it.

**Rationale:** The fundamental problem is a debounce timing paradox. Any debounce long enough to avoid false triggers (≥3s) consumes most or all of the physical decay. Lowering debounce causes false transitions on noise spikes. Post-hoc analysis sidesteps this entirely: the full disturbance+decay waveform is already in the ring buffer. We just need to find the decay region after the fact.

**Mechanism:**
1. DISTURBED→IDLE transition fires when `accel_err_var < K_LOW` for `DISTURBED_EXIT_DEBOUNCE` samples
2. At transition, scan the stored buffer backward from the end
3. Find the peak amplitude (maximum |tilt|) — this marks the approximate start of decay
4. Track the declining envelope forward from that peak
5. The decay region = peak to buffer end (or until amplitude stops declining)
6. Run FFT and damping regression on this identified region

### D2: Linear regression on log(peak amplitudes) over 3-peak log decrement

**Decision:** Replace the `kDecaySpan=3` log decrement with ordinary least squares (OLS) linear regression on `ln(|peak_amplitude|)` vs `time`.

**Rationale:** The 3-peak log decrement uses only x1 (peak at max) and x2 (peak 3 cycles later). This is extremely sensitive to individual peak measurement error — a single noisy peak shifts the result significantly. For lightly-damped systems (typical for tree branches, ζ ≈ 0.01–0.05), many peaks are available in the decay envelope. Linear regression on all peaks provides:
- Statistical averaging across N peaks instead of relying on 2
- Outlier resistance (bad peaks diluted by good ones)
- Confidence indication: R² of the fit indicates data quality

**Formula:** For underdamped free decay, amplitude envelope ≈ A₀·e^(−ζωₙt). Taking log: ln(A) = ln(A₀) − ζωₙt. The slope of the regression line = −ζωₙ. Combined with ωₙ from FFT: ζ = −slope / ωₙ.

**Minimum peaks:** Require ≥3 peaks for regression (2 yields a perfect fit with no error estimate). If <3 peaks, output ζ=0.

### D3: Decay region identification via peak envelope analysis

**Decision:** Identify the decay start by finding the global peak amplitude in the stored buffer, then verify monotonically declining envelope forward from that point.

**Algorithm:**
1. Detect all peaks/troughs in the stored buffer using existing peak detection (with new thresholds)
2. Find the extremum with maximum |amplitude| — this is the candidate decay start
3. From this peak forward, collect successive peaks where |amplitude| is declining
4. Stop collecting when amplitude increases (re-excitation) or the buffer ends
5. The collected peaks form the decay envelope for regression
6. The sample range from decay start to last declining peak defines the FFT window

**Edge case — no clear decay:** If the global max is near the buffer end (insufficient decay data), or fewer than 3 declining peaks follow the max, damping output is 0.0f. Frequency is still computed from whatever data is available.

### D4: PEAK_MIN_SPACING=2, PEAK_MIN_AMPLITUDE=0.5°

**Decision:** Change Kconfig defaults for peak detection parameters.

**PEAK_MIN_SPACING 5→2:**
- At 26Hz ODR, spacing=5 limits detection to signals ≤2.6Hz (half-period ≥5 samples → f ≤ 26/10 = 2.6Hz)
- Spacing=2 supports up to 6.5Hz (half-period ≥2 samples → f ≤ 26/4 = 6.5Hz)
- For 12Hz bench samples: half-period ≈1.08 samples — still marginal, but spacing=2 catches harmonics and the fundamental's beat patterns
- Spacing=1 risks detecting noise as peaks; spacing=2 is the practical minimum at 26Hz ODR

**PEAK_MIN_AMPLITUDE 2°→0.5°:**
- Late-decay peaks in lightly-damped systems routinely fall below 2°
- At ζ=0.02, a 10° initial amplitude decays below 2° after ~16 cycles — losing most of the useful envelope
- 0.5° threshold captures ~30 cycles at same parameters, giving much better regression fit
- IMU noise floor at 26Hz is ~0.05° RMS (complementary filter), so 0.5° provides 10× SNR margin

### D5: DISTURBED→IDLE exit debounce

**Decision:** Add `CONFIG_MONITOR_DISTURBED_EXIT_DEBOUNCE` with default value of 64 samples (~2.5s at 26Hz).

**Rationale:** Without debounce, brief dips in `accel_err_var` during active disturbance cause premature DISTURBED→IDLE transitions, triggering unnecessary analysis on incomplete data. The debounce ensures the branch has genuinely settled.

**Value selection:**
- 64 samples = 2.46s at 26Hz
- Short enough to not consume the entire decay (unlike the old 128-sample FREE_DECAY debounce at 4.9s)
- Long enough to reject transient dips in accel_err_var during wind gusts
- The debounce is on `accel_err_var < K_LOW`, not `K_MID` — K_LOW is a lower threshold, so by the time it's sustained for 2.5s, the branch is definitively settled
- The decay data is preserved in the buffer regardless of debounce duration — post-hoc analysis looks backward

### D6: Impact on logger and dashboard

**Decision:** Remove `FREE_DECAY` from `NodeState` enum. Logger and dashboard drop the enum variant.

**Impact:**
- `logger.cpp`: Remove 2 `FREE_DECAY` string mapping branches (lines 112-113, 152-153). No new branches needed — DISTURBED exit results carry `state=DISTURBED`.
- `dashboard.cpp`: Remove FREE_DECAY state display (lines 496, 724-725). Two-state display only.
- CSV output: `FREE_DECAY` rows no longer produced. Analysis results (freq, damping) now appear in the DISTURBED→IDLE transition row.
- **Breaking:** Log parsers expecting `FREE_DECAY` state string will need update.

### D7: Re-excitation handling

**Decision:** No separate re-excitation logic needed. IDLE→DISTURBED re-trigger handles it naturally.

**Current behavior:** FREE_DECAY→DISTURBED on `accel_err_var > K_HIGH` (re-excitation during decay).

**New behavior:** After DISTURBED→IDLE, if the branch is re-excited, the normal IDLE→DISTURBED transition fires. The previous disturbance event's analysis is already complete. No state is lost.

**Edge case — re-excitation during debounce:** If `accel_err_var` exceeds `K_HIGH` during the DISTURBED exit debounce (while counting consecutive samples below K_LOW), the debounce counter resets. The node stays DISTURBED and continues accumulating data. This is correct behavior.

### D8: Kconfig changes

**Remove:**
- `CONFIG_MONITOR_K_MID_X100` — no intermediate threshold without FREE_DECAY
- `CONFIG_MONITOR_FREE_DECAY_DEBOUNCE` — replaced by DISTURBED exit debounce
- `CONFIG_MONITOR_FREE_DECAY_TIMEOUT_S` — no FREE_DECAY state to timeout

**Add:**
- `CONFIG_MONITOR_DISTURBED_EXIT_DEBOUNCE` — default 64, range 1–1000

**Modify defaults:**
- `CONFIG_MONITOR_PEAK_MIN_AMPLITUDE` — default 2→0 (representing 0.5° via scaling, or change to x10 scaled integer)
- `CONFIG_MONITOR_PEAK_MIN_SPACING_SAMPLES` — default 5→2

**Note on PEAK_MIN_AMPLITUDE scaling:** Current Kconfig is integer degrees (default 2). To represent 0.5°, either change to x10 scaled integer (`CONFIG_MONITOR_PEAK_MIN_AMPLITUDE_X10`, default 5) or use x100. This is an implementation detail resolved during task execution.

## Risks / Trade-offs

### R1: Peak amplitude as decay start proxy
Finding the global max amplitude assumes the decay starts at the strongest oscillation. If the disturbance contains multiple distinct excitation events (e.g., two wind gusts), the global max may be in the middle of the buffer with no clean decay following it. **Mitigation:** The declining-envelope check (D3 step 3-4) handles this — if peaks after the max don't decline, we get fewer usable peaks and possibly ζ=0. This is a graceful degradation, not a failure.

### R2: 0.5° amplitude threshold and noise
At 0.5° threshold, we're closer to the noise floor (~0.05° RMS). In very quiet conditions or long decays, noise peaks could be falsely detected. **Mitigation:** The 10× SNR margin is adequate. Peak detection requires both neighbors to be lower (3-point test), which rejects most isolated noise spikes. The linear regression naturally averages out noise-induced scatter in peak amplitudes.

### R3: 26Hz ODR limits high-frequency detection
At 26Hz, the Nyquist frequency is 13Hz. Signals at 12Hz have only ~2.17 samples per cycle — barely above Nyquist. Peak detection with spacing=2 may miss some peaks. **Mitigation:** This is a pre-existing limitation of the 26Hz ODR, not introduced by this change. For bench samples at 12Hz, the FFT will still resolve the frequency correctly even if peak-based damping is less accurate. If high-frequency bench testing is a priority, ODR should be increased (separate change).

### R4: Regression sensitivity to decay region selection
If the identified decay region includes non-decaying data (e.g., forced vibration before the decay start), the regression fit will be poor. **Mitigation:** The algorithm starts from the global peak and only includes declining peaks, filtering out forced-vibration portions. R² of the fit (computed but not currently exported) could be added as a quality metric in a future change.

### R5: Slightly more compute at DISTURBED→IDLE exit
Post-hoc analysis adds one extra pass through the buffer (peak detection + envelope tracking) plus linear regression. At 7800 samples, this is ~0.3ms on ESP32-S3 at 240MHz. **Impact:** Negligible. Runs once per disturbance event, not per sample.
