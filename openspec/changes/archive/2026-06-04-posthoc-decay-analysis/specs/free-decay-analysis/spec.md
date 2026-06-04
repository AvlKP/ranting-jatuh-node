# free-decay-analysis Delta Specification

**Change:** posthoc-decay-analysis
**Base Spec:** openspec/specs/free-decay-analysis/spec.md

## REMOVED Requirements

### Requirement: FREE_DECAY State Entry
**Reason**: FREE_DECAY state removed. Decay detected post-hoc from DISTURBED buffer.
**Migration**: Decay analysis triggered on DISTURBED→IDLE transition instead of explicit FREE_DECAY entry.

### Requirement: FREE_DECAY Buffer Initialization
**Reason**: No separate decay buffer. DISTURBED buffer used retroactively.
**Migration**: Post-hoc analysis identifies decay region within the existing DISTURBED buffer.

### Requirement: Per-Axis FFT in FREE_DECAY
**Reason**: FFT now runs post-hoc on DISTURBED→IDLE transition.
**Migration**: Same FFT computation, different trigger. See added requirement "Per-Axis FFT on Post-Hoc Decay Region".

### Requirement: Per-Axis Damping Ratio in FREE_DECAY
**Reason**: Damping now computed post-hoc using envelope regression.
**Migration**: See envelope-damping-regression spec for replacement approach.

### Requirement: FREE_DECAY Exit to IDLE
**Reason**: No FREE_DECAY state. DISTURBED→IDLE with debounce replaces this.
**Migration**: DISTURBED exit debounce defined in node-state-machine delta spec.

### Requirement: FREE_DECAY Re-excitation
**Reason**: No FREE_DECAY state. Re-excitation handled by IDLE→DISTURBED re-entry.
**Migration**: If disturbance resumes after IDLE, normal IDLE→DISTURBED transition occurs.

### Requirement: FREE_DECAY Timeout
**Reason**: No FREE_DECAY state. DISTURBED has buffer refresh mechanism for prolonged disturbances.
**Migration**: Buffer refresh in DISTURBED publishes intermediate sway; no timeout needed.

### Requirement: FREE_DECAY Result Publication
**Reason**: Results now published on DISTURBED→IDLE transition.
**Migration**: Same data (natural_freq, damping_ratio), different trigger point.

## ADDED Requirements

### Requirement: Post-Hoc Decay Analysis Trigger
The monitor SHALL trigger post-hoc decay analysis (FFT + damping) on the stored DISTURBED buffer when transitioning from DISTURBED to IDLE. The analysis SHALL retroactively identify the decay region within the buffer.

#### Scenario: Normal transition triggers analysis
- **WHEN** the node transitions from `DISTURBED` to `IDLE` (debounce satisfied)
- **THEN** the system SHALL invoke post-hoc decay detection on the stored DISTURBED buffer
- **THEN** the system SHALL compute FFT and damping on the identified decay region
- **THEN** the results SHALL be published immediately as part of the DISTURBED→IDLE MonitorResult

#### Scenario: Buffer refresh during long disturbance
- **WHEN** the DISTURBED buffer is refreshed due to capacity
- **THEN** intermediate sway statistics SHALL be published
- **THEN** FFT and damping SHALL NOT be computed on the refreshed buffer
- **THEN** FFT and damping SHALL only be computed on the final DISTURBED→IDLE transition

### Requirement: Per-Axis FFT on Post-Hoc Decay Region
The monitor SHALL compute FFT on roll and pitch signals independently from the retroactively identified decay region of the DISTURBED buffer, producing separate natural frequency results.

#### Scenario: Decay region identified
- **WHEN** post-hoc analysis identifies a decay region in the DISTURBED buffer
- **THEN** the system SHALL compute FFT on the roll decay segment, producing `natural_freq_roll_hz`
- **THEN** the system SHALL compute FFT on the pitch decay segment, producing `natural_freq_pitch_hz`
- **THEN** each FFT SHALL use the same adaptive window sizing as the existing FFT implementation (Welch for ≥1024, zero-pad for shorter)

#### Scenario: No decay region identified
- **WHEN** post-hoc analysis fails to identify a valid decay region in the DISTURBED buffer
- **THEN** `natural_freq_roll_hz` SHALL be set to 0.0f
- **THEN** `natural_freq_pitch_hz` SHALL be set to 0.0f
- **THEN** `damping_ratio_roll` and `damping_ratio_pitch` SHALL be set to 0.0f
