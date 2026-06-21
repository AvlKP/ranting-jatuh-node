## Context

The node's methodology is currently documented only in fragments: 24 capability specs define *requirements*, `ARCHITECTURE.md` describes the *implementation* wiring, `docs/natural-frequency-pipeline.md` traces *one* algorithm in depth, and the reasoning behind technique selection is embedded in inline code comments. A reader who wants to understand the engineering methodology — without reading embedded C++ — has no entry point. A prior documentation change (`document-natural-frequency-pipeline`) established the convention of a `docs/` deep-dive and the OpenSpec documentation-only change pattern (no capabilities, no spec delta). This change generalizes that approach from a single pipeline to the full set of techniques across the system, with an explicit constraint: **techniques and methodologies only — no framework, language, memory-layout, or task-runtime implementation details**.

Stakeholders: structural-health / signal-processing reviewers who do not read embedded code; new contributors; and future maintainers evaluating whether a technique should be swapped out.

## Goals / Non-Goals

**Goals:**
- Document every technique and methodology the node applies, grouped by theme, at a concept level
- For each technique record: what it is, why it was selected, which alternative was rejected and why, and what it achieves
- Keep the document implementation-agnostic: no ESP-IDF APIs, no C++ constructs, no buffer/memory sizes, no task/priority/core runtime detail
- Provide a selection-rationale layer that the existing specs (which state requirements) and ARCHITECTURE.md (which describes implementation) do not provide
- Cross-reference existing specs and `docs/natural-frequency-pipeline.md` so readers can drill down
- Make the methodology reviewable by readers outside the embedded domain

**Non-Goals:**
- Duplicate the algorithmic trace already in `docs/natural-frequency-pipeline.md` — reference it instead
- State normative requirements (that is the role of specs)
- Describe file layout, build system, pin assignments, or runtime task wiring (that is the role of `ARCHITECTURE.md`)
- Change any code, config, or spec behavior
- Cover server-side analysis (out of scope for the node)

## Decisions

1. **Single document, themed sections**: `docs/techniques-and-methodologies.md` with sections per theme (sensor fusion, disturbance detection, event state machine, modal analysis, failure detection, reliability & data integrity, power & network strategy, validation methodology, engineering invariants). Rationale: a reader can jump to the technique family they care about; alternatives (one file per technique, or a single long narrative) either fragment cross-references or bury structure.

2. **Per-technique entry shape**: each technique is a sub-section with a short "what / why selected / alternative rejected / what it achieves" structure. Rationale: makes the selection logic explicit and consistent; the precedent doc used a freer structure which works for one pipeline but not for a catalogue of unrelated techniques.

3. **Implementation-agnostic constraint, enforced by section rule**: every section is written so a reader never needs to know the framework, language, memory sizes, or task runtime. Where a technique's rationale touches real-time determinism, it is framed at the concept level (e.g., "the sampling path performs bounded work only") without naming runtime primitives. Rationale: honors the explicit scope constraint and keeps the document useful to non-embedded reviewers.

4. **Cross-reference, do not duplicate**: the modal-analysis section summarizes the technique and links to `docs/natural-frequency-pipeline.md` for the full trace; specs are linked for normative requirements. Rationale: avoids drift between documents; the pipeline doc already owns the FFT/damping detail.

5. **No spec delta**: the proposal lists zero new and zero modified capabilities, so no `specs/<name>/spec.md` is created, mirroring the `document-natural-frequency-pipeline` precedent. Rationale: documentation-only changes with no behavioral requirement have nothing to put in a spec.

6. **Include an explicit "alternatives considered" layer**: for each technique name the rejected alternative (e.g., Madgwick/Kalman/EKF for orientation; raw-threshold vs TKEO for disturbance; streaming vs post-hoc analysis; persistent vs on-demand WiFi). Rationale: the reasoning behind a selection is only legible when the rejected option is named.

7. **Engineering invariants as a concept-level section**: include a section on the deterministic-sampling, fixed-capacity-storage, compile-time-assertion, scaled-integer-configuration, and sampling-isolation disciplines — framed as methodology, not as memory or runtime detail. Rationale: these are genuine engineering techniques with selection rationale; excluding them would omit a core methodology. The constraint is on *implementation detail*, not on naming the discipline.

## Risks / Trade-offs

- [Risk] Document drifts as techniques evolve → Mitigation: cross-reference specs (source of truth for behavior) and `ARCHITECTURE.md` (source of truth for structure); add a "last verified" date; flag the doc for review in any future technique change.
- [Risk] Concept-level framing hides detail a maintainer needs → Mitigation: every section links to the deeper source (spec, pipeline doc, or ARCHITECTURE.md) for readers who need the implementation trace.
- [Risk] "Alternatives considered" may be reconstructed rather than recalled if the original decision was never recorded → Mitigation: only document alternatives that are evidenced by existing library code (e.g., present-but-not-instantiated filters) or by spec history; mark uncertain rationale as such rather than inventing it.
- [Risk] Section boundary with `docs/natural-frequency-pipeline.md` blurs → Mitigation: modal-analysis section is a summary + link; the pipeline doc remains the single source for the FFT/damping trace.
