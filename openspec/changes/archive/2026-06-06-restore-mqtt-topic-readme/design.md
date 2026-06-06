## Context

The project currently uses flat MQTT topic names defined via Kconfig macros. Topics like `CONFIG_LOGGER_MQTT_TOPIC_PARAMETERS` (default `"ranting/parameters"`) and `CONFIG_LOGGER_MQTT_TOPIC_FAILURES` (default `"ranting/failures"`) are shared across all nodes. With only one node, this works. With multiple nodes, the server receives interleaved data from all nodes on the same topic with no way to attribute messages to a specific physical device.

The README.md is a 5-line TODO list from early development:
```
1. Transition to RTOS and esp_event.h API [x]
2. Add wind/storm state to trigger sway and damping ratio calculation
3. Use IMU's FIFO to reduce awake time
4. Do temperature calibration on IMU
```
Item 1 is completed and the rest are tracked elsewhere. The file gives no useful information to someone encountering the project.

## Goals / Non-Goals

**Goals:**
- Every node SHALL have a unique identifier, auto-generated on first boot, persisted in NVS, overridable via Kconfig
- All MQTT publish topics SHALL include the node ID as a path segment: `ranting/{node_id}/{datatype}`
- Dashboard status API SHALL expose the node ID
- README SHALL document project purpose, hardware, architecture, build instructions, configuration, and implementation status

**Non-Goals:**
- No server-side changes beyond MQTT subscription wildcard migration
- No changes to MQTT payload format (CSV data, failure strings unchanged)
- No changes to how data is collected or processed (monitor, IMU driver unchanged)
- No multi-file documentation beyond README.md

## Decisions

### 1. Node ID source: random adjective-noun vs UUID vs MAC

**Decision:** Adjective-noun with `esp_random()` from hardware RNG.

**Rationale:** UUID (36 chars) and MAC (17 chars) are less human-friendly for debugging and dashboard display. Adjective-noun (e.g. `quiet-pine`) is ~10 chars, memorable, and human-readable. Word pools of ~64 adjectives and ~64 nouns give 4096 combinations — sufficient for the scale of deployment (tens of nodes). Collision probability is negligible for this use case.

### 2. ID resolution priority: Kconfig → NVS → generate

**Decision:** Check `CONFIG_LOGGER_NODE_ID` first. If non-empty (factory provisioning), use it directly and skip NVS. If empty, read from NVS. If NVS key missing, generate new random ID, write to NVS, use it.

**Rationale:** Kconfig override enables production-line provisioning without NVS interaction. NVS persistence ensures ID survives reboot. Auto-generation ensures zero-config setup for development.

### 3. Topic construction: dynamic format string vs static cache

**Decision:** `BuildTopic(datatype)` formats `ranting/{node_id}/{datatype}` into a static buffer on each call. `GetTopic(datatype)` wrapper returns cached result when `datatype` matches previous call.

**Rationale:** Topic strings are short (~40 chars max), formatting is trivial. Caching avoids redundant `snprintf` when multiple calls use same datatype (common case: periodic parameter publishes). No dynamic allocation.

### 4. NVS namespace and key

**Decision:** Reuse existing NVS namespace `"logger"` with key `"node_id"`.

**Rationale:** Logger already initializes NVS in its MQTT init path. No new namespace needed. Single key, simple read/write.

### 5. README structure

**Decision:** Single-file README.md mirroring AGENTS.md structure: Purpose, Hardware, Architecture, Build Instructions, Configuration, Implementation Status, Future Work.

**Rationale:** AGENTS.md already captures project context consistently. README should present same information in human-readable form. Single file keeps discoverability high.

## Risks / Trade-offs

- **Node ID collision (4096 combinations):** Low risk for small deployments. Mitigation: future enhancement can add a counter suffix if collisions become an issue.
- **NVS write wear:** Written once on first boot, then only if Kconfig override changes. Negligible wear on NVS flash partition.
- **Server subscription breakage:** Old server subscribing to `ranting/parameters` won't see new node messages. **BREAKING change.** Mitigation: server must migrate to wildcard `ranting/+/parameters` which captures both old and new format topics during transition.
- **Readme stale again:** Documentation becomes outdated as code changes. Mitigation: link to active specs in openspec/specs/ for detailed requirements; keep README at architectural overview level.
