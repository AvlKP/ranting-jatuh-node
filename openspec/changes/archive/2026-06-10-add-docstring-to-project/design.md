## Context

The project has two codebases — C++ firmware (ESP-IDF) and Python reference implementation — with significant algorithmic overlap but inconsistent documentation. A codebase audit revealed vestigial code paths from older design iterations that remain in the codebase but are superseded by newer implementations. Rather than deleting this code (which carries build risk and is handled by a separate removal proposal), this change focuses purely on adding documentation — docstrings for active code, @deprecated tags for superseded code.

Current state:
- Only `adaptive_complementary_filter.hpp` has partial Doxygen comments; no other file has systematic documentation.
- Python files have minimal module docstrings; no function-level documentation exists except inline comments.
- Superseded code (ChebyshevHpf, old monitor modal analysis, dead class members) has no indication that it is vestigial.
- Algorithm derivation (TKEO, damping OLS, decay onset) lives only in the Python file comments; C++ duplicates lack explanation.

## Goals / Non-Goals

**Goals:**
- Document all active C++ public headers with Doxygen `@brief`, `@param`, `@return`, `@note`, `@see` tags.
- Tag all superseded project-specific code with `@deprecated` Doxygen tags explaining why it is dead and what replaced it.
- Document all Python public API with Google-style docstrings, including Python-only reference functions.
- Add algorithm derivation notes for signal processing functions in both languages.
- Ensure docstrings include cross-references between Python and C++ implementations.

**Non-Goals:**
- Deleting any files, methods, structs, Kconfig keys, or class members. Code removal is a separate proposal.
- Generating Doxygen HTML or PDF output (build system integration is separate).
- Documenting third-party or managed components under `managed_components/`.
- Documenting ESP-IDF SDK APIs (out of scope, external dependency).
- Documenting `components/filter/` beyond `adaptive_complementary_filter.hpp` and `filter_math.hpp`. Madgwick, Kalman, EKF, and basic Complementary filters are generic DSP building blocks, not project-specific.
- Changing any algorithmic behavior — documentation-only.
- Adding docstrings to internal-only FreeRTOS task functions, ISR handlers, or logging macros.
- Creating a documentation style guide (project uses Doxygen as stated in AGENTS.md).

## Decisions

### Decision 1: @deprecated tags instead of code removal
Superseded project-specific code receives `@deprecated` Doxygen tags rather than being deleted. This documents the design evolution without risking build breakage or requiring hardware verification. A separate proposal will handle actual code removal. Generic filter code (Madgwick, Kalman, EKF, Complementary) is left entirely untouched — it is not project-specific and has no consumer to confuse.

**Alternatives considered:**
- Remove dead code: Requires build verification, test cleanup, and carries risk of missing a consumer. Separated into its own proposal.
- Leave dead code undocumented: Future readers won't know it's vestigial, may waste time understanding dead paths.
- Remove AND deprecate: Redundant work; @deprecated is sufficient for the documentation pass.

### Decision 2: Doxygen `@see` cross-references to Python
C++ algorithm functions (TKEO, decay onset, peak-hold envelope, damping OLS) include `@see imu_algorithms/_<module>.py::<function>` tags. This traces the reference implementation lineage and helps maintain algorithmic equivalence.

**Alternatives considered:**
- No cross-references: Loses traceability between Python and C++.
- Bidirectional references: Python referencing C++ file paths is fragile since the C++ structure may change. Python side uses plain text references only.

### Decision 3: `@ingroup` tags for component grouping
Each component header uses `@ingroup <component>` tags (e.g., `@ingroup monitor`, `@ingroup filter`, `@ingroup logger`). This enables automatic Doxygen module grouping without manual grouping config.

**Alternatives considered:**
- Doxygen `@defgroup` with separate group definitions: More verbose, requires maintaining a separate groups file.
- No grouping: Doxygen output becomes a flat, unsorted list.

### Decision 4: Google-style docstrings for Python (not NumPy-style)
The Python reference uses Google-style because it is more compact and equally readable in plain text (the primary use case is reading source, not generating Sphinx docs). Existing comments already follow an informal Google-like pattern.

**Alternatives considered:**
- NumPy-style: More verbose, better for Sphinx but overkill for this project.
- ReST: Even more verbose, no added benefit.

### Decision 5: Python-only functions get full docstrings, not @deprecated
Functions like `classify_event`, `is_dynamic`, `extract_active_region`, `extract_frequency_zc`, and `extract_frequency_pk` exist only in the Python reference and were intentionally not ported to C++. They are reference implementations, not dead code. They receive full Google-style docstrings explaining the algorithm and noting they serve as reference for future C++ porting.

**Alternatives considered:**
- Skip documenting Python-only functions: Loses algorithmic knowledge.
- Mark as @deprecated in Python: Misleading — they are reference implementations, not obsolete code.

## Risks / Trade-offs

- **Risk**: @deprecated tags become stale if the replacement changes. → **Mitigation**: Tags reference the replacement API by name (`AnalyzeImuEvent`, `DspDisturbanceDetector`), which is unlikely to change without a corresponding update to the deprecated path.
- **Risk**: Doxygen comments becoming stale if code changes. → **Mitigation**: Docstrings are placed close to declarations; the AGENTS.md standard mandates keeping docs current. No automated enforcement yet — follow-up change candidate.
- **Trade-off**: Inline comments in `.cpp` files add maintenance burden but clarify non-obvious algorithm steps. The value of explaining FFT windowing and Hann window choice outweighs the maintenance cost.
- **Trade-off**: @deprecated tags increase file size but prevent wasted debugging time. ~5 tags in `monitor.hpp`, ~3 in `monitor.cpp`, ~1 in `chebyshev_hpf.hpp`, ~3 in Kconfig. Total overhead: <20 lines.

## Open Questions

- Should Doxygen be integrated into the ESP-IDF build (`idf.py docs`)? Deferred — this change only adds the docstrings, not the build integration.
- Should the Python reference include a Sphinx `conf.py`? Deferred — not needed until the reference grows significantly.
