## Context

The current README.md is a 5-line TODO list:
1. Transition to RTOS and `esp_event.h` API [x]
2. Add wind/storm state to trigger sway and damping ratio calculation
3. Use IMU's FIFO to reduce awake time
4. Do temperature calibration on IMU

This is neither informative nor accurate. The project is an ESP32-S3-based IoT node for tree branch monitoring with LSM6DS3 IMU, SD card logging, MQTT publishing, and an HTTP dashboard. The TODO items are either completed (item 1) or tracked in other project artifacts. Readme should document what the project IS, not what remains to do.

## Goals / Non-Goals

**Goals:**
- Replace the TODO list with a proper README that describes the project's purpose, architecture, and usage.
- Include: project overview, hardware requirements, component architecture, build instructions, configuration, and current implementation status.
- Keep it concise and technically accurate.

**Non-Goals:**
- No code changes. No spec changes. Pure documentation.
- Not a full user manual or API reference.
- Not replacing AGENTS.md, Kconfig help text, or source code comments.

## Decisions

**Decision: Single-file README.md rewrite**
- Alternatives: Multi-file docs (`docs/` directory), wiki. Multiple files add maintenance burden for a single-repo embedded project.
- Rationale: A single README is the standard entry point. Additional docs belong in source code (Doxygen) or Kconfig help text.

**Decision: Include current TODO items as "Future Work" section**
- Alternatives: Remove entirely, move to GitHub issues.
- Rationale: The remaining TODO items (FIFO usage, temp calibration, wind state) are still relevant to the project roadmap. List them as future work so they're not lost.

**Decision: Mirror existing `AGENTS.md` structure for consistency**
- Alternatives: Write independent structure.
- Rationale: AGENTS.md already has good project overview and architecture sections. README should be consistent but target human developers rather than AI agents.

## Risks / Trade-offs

- [Docs drift] README may become outdated again as features change. Mitigation: Reference Kconfig and source files where authoritative detail lives; README provides overview only.
