## Context

All nodes publish to shared flat topics (`ranting/parameters`, `ranting/failures`, `ranting/verify`). The server receives data from multiple nodes on the same topics with no source identification. The node is publish-only (no MQTT subscribe), so server-side configuration is limited to compile-time (Kconfig) or provisioning (NVS flash). NVS is already initialized in `logger_mqtt.cpp` for WiFi PHY calibration — we can reuse this for persisting node ID.

Current topic construction uses `CONFIG_LOGGER_MQTT_TOPIC_PARAMETERS`, `CONFIG_LOGGER_MQTT_TOPIC_FAILURES`, and `CONFIG_APP_VERIFY_MQTT_TOPIC` directly from sdkconfig. These are compile-time constants.

## Goals / Non-Goals

**Goals:**
- Every publish topic includes a unique node identifier: `ranting/{node_id}/parameters`
- Node ID auto-generated on first boot, persisted in NVS, survives reboots
- Node ID overridable via Kconfig for known deployments
- Dashboard status API exposes the node ID for operator visibility
- Backward compatible: server can use wildcard `ranting/+/parameters` during migration

**Non-Goals:**
- Server-side dynamic reconfiguration (requires MQTT subscribe, out of scope)
- Changing MQTT client ID (stays `ranting-logger` or configurable separately)
- Adding node_id to JSON/CSV payload bodies (topics already encode the ID)
- Multi-node discovery or coordination

## Decisions

**Decision 1: Generate random adjective-noun ID at first boot**

Use a fixed word list (~64 adjectives, ~64 nouns) and combine them with `esp_random()` to produce IDs like `quiet-pine`, `bold-oak`. Format: `{adjective}-{noun}`. Stores in NVS namespace `logger` key `node_id`. On boot, read NVS first — if present, use it; otherwise generate new and store.

*Alternative considered: MAC-based ID.* Rejected because MAC is not human-readable and leaks hardware identity. Adjective-noun IDs are memorable and user-friendly.

*Alternative considered: UUID.* Rejected because it's long and ugly in MQTT topics. Docker-style names are concise and readable.

**Decision 2: Kconfig default, NVS override**

Kconfig `CONFIG_LOGGER_NODE_ID` defaults to empty string `""`. If empty at runtime, use NVS value (the generated or previously stored ID). If Kconfig is non-empty, use Kconfig value and skip NVS read/generate. This allows:
- Default: random generation (Kconfig empty → NVS generate)
- Provisioning: set `CONFIG_LOGGER_NODE_ID="pole-12"` at flash time
- Resetting: erase NVS partition to regenerate random ID

*Alternative considered: only NVS, no Kconfig.* Rejected because factory provisioning needs a way to pre-set IDs without NVS access.

**Decision 3: Topic format `ranting/{node_id}/{datatype}`**

Build topics dynamically at call sites rather than compile-time constants. Replace `CONFIG_LOGGER_MQTT_TOPIC_PARAMETERS` with a function that builds the full topic string from the base prefix and node ID. Use a static buffer to avoid heap allocation.

*Alternative considered: string concatenation in each publish function.* Rejected because it's error-prone. Centralize topic construction in one helper.

**Decision 4: NVS namespace `logger`, key `node_id`**

Reuse existing NVS init path. The `InitNvs()` function already handles partition erase and retry. Store `node_id` as a string blob.

**Decision 5: Topic Kconfig options become suffixes only**

Keep `CONFIG_LOGGER_MQTT_TOPIC_PARAMETERS` and `CONFIG_LOGGER_MQTT_TOPIC_FAILURES` but change their defaults from `ranting/parameters` to `ranting/{node_id}/parameters`. Or, better: change Kconfig to suffix-only (`parameters`, `failures`), and construct the full topic at runtime. This keeps Kconfig simple and avoids hardcoding `ranting/` in multiple places.

*Alternative considered: keep full topic in Kconfig and append node ID at runtime.* Rejected because it's confusing — users would need to remember to include the prefix.

**Decision: Use a shared prefix string `ranting/` as constant, append node_id and datatype at runtime.**

New constant: `kTopicBasePrefix = "ranting/"`. New helper: `BuildTopic(char* buf, size_t buf_size, const char* node_id, const char* datatype)` produces `ranting/{node_id}/{datatype}`.

## Risks / Trade-offs

- **Risk: NVS corruption loses node ID** → Mitigation: Node generates new random ID on corruption (treated as new node). Server sees new ID and treats it as a newly deployed node.
- **Risk: Topic name length exceeds MQTT spec (65535 bytes)** → Mitigation: Node ID capped at 32 chars (far below limit). Total topic ~50 chars max.
- **Risk: Two nodes generate same random ID** → Mitigation: 64×64 = 4096 unique combinations. With random seeding from hardware RNG, collision probability is extremely low for typical deployments (≤100 nodes).
- **Risk: Breaking change for existing server** → Mitigation: Server should update topic subscriptions to wildcard pattern `ranting/+/parameters`. Old nodes (without this change) continue publishing to `ranting/parameters` — server can handle both during migration.

## Migration Plan

1. Deploy firmware to nodes with new topic format
2. Update server MQTT subscriptions to `ranting/+/parameters`, `ranting/+/failures`, `ranting/+/verify`
3. Server routes data by extracting `node_id` from topic
4. Old firmware (no node ID) publishes to `ranting/parameters` — server receives on wildcard and treats as legacy node

No rollback needed: old firmware continues to work on `ranting/parameters` (no prefix), new firmware uses `ranting/{id}/parameters`. Server wildcard captures both.

## Open Questions

- Should MQTT client ID also include node_id for broker-side identification? (Currently `ranting-logger` — could become `ranting-{node_id}` for consistency)
- Server-side: Is there a requirement for the server to know node_id-to-location mapping? (Out of scope for this change but informs future provisioning workflow)
