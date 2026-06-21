## Why

The system applies a chain of signal-processing, state-machine, reliability, and power-management techniques to infer branch structural health from IMU and acoustic-emission data. Each technique was selected over alternatives for a concrete reason (latency, power, robustness, data integrity), but that reasoning lives scattered across 24 specs, ARCHITECTURE.md, and inline code comments. There is no single document that explains *which* techniques are used, *why* each was chosen, and *what* it achieves at a concept level — independent of any specific firmware framework, language, or runtime. New contributors must reverse-engineer the methodology from implementation files. A technique-level reference decouples the "what/why" of the methodology from the "how" of the implementation, making the system reviewable by structural-engineering and signal-processing readers who do not read embedded C++.

## What Changes

- Create `docs/techniques-and-methodologies.md`: a concept-level reference documenting every technique and methodology used by the node, with the reasoning and selection logic for each.
- Scope the document to **techniques and methodologies only** — no framework-specific, language-specific, memory-layout, or task-runtime implementation details.
- For each technique cover: what it is, why it was selected, which alternative was rejected and why, and what it achieves.
- Group techniques into themed sections: sensor fusion, disturbance detection, event state machine, modal analysis, failure detection, reliability & data integrity, power & network strategy, validation methodology, and engineering invariants.
- Cross-reference the existing specs and `docs/natural-frequency-pipeline.md` for readers who want the deeper algorithmic trace.

## Capabilities

### New Capabilities
<!-- This is a documentation-only change. No new functional capabilities. -->
None.

### Modified Capabilities
None.

## Impact

- Affected files: New file `docs/techniques-and-methodologies.md`
- No code changes, no API changes, no dependency changes, no spec-level behavior changes
- References existing specs: `adaptive-complementary-filter`, `free-decay-analysis`, `imu-event-analysis`, `envelope-damping-regression`, `noise-gate`, `node-state-machine`, `ae-spectral-detector`, `free-fall-debounce`, `accel-error-state-detection`, `imu-calibration`, `imu-mount-transform`, `network-strategy`, `network-task`, `sd-upload-queue`, `embedded-runtime-safety`, `monitor-mutex-safety`, `startup-time-sync`, `node-id-topic-prefix`
- References existing docs: `ARCHITECTURE.md`, `docs/natural-frequency-pipeline.md`, `mqtt_interface.md`
- Audience: structural-health and signal-processing reviewers, new contributors, and anyone evaluating the methodology without reading embedded implementation
