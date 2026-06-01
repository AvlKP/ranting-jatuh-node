## Context

Currently, the tree branch monitoring IoT node continuously calculates the natural frequency, damping ratio, and sway amplitude using a rolling 5-minute time window. Since disturbances are rare, processing this quiet data results in a DC bias for these parameters, leading to inaccurate baseline measurements and inefficient CPU/power usage.

## Goals / Non-Goals

**Goals:**
- Eliminate the DC bias in natural frequency, damping ratio, and sway amplitude calculations by only computing them when valid disturbances happen.
- Define a clear set of states (`IDLE` and `DISTURBED`) for the node.
- Keep execution deterministic per `/realtime-cpp` by calculating an O(1) rolling variance instead of batch processing during `PushSample()`.

**Non-Goals:**
- Deep sleep implementation is explicitly excluded (to favor rapid iteration as per project rules).
- Use of dynamic memory (`std::vector` or `new`/`delete`). The state machine and buffers will strictly use `std::array`.

## Decisions

**1. State Machine & Hardware Constraints:**
- *Decision:* Use a simple C++ enum (`enum class NodeState { IDLE, DISTURBED };`) with a statically allocated short buffer (`std::array<float, CONFIG_MONITOR_SHORT_BUFFER_SIZE>`).
- *Rationale:* Avoids the complexity and heap fragmentation risk of state chart libraries or dynamic arrays. Fits strict zero-overhead real-time constraints.

**2. Kconfig Configuration & Float Scaling:**
- *Decision:* Define parameters (`K_IDLE`, `K_DISTURBED`, `N_DPAD`, `short_buffer_size`) via ESP-IDF Kconfig (`Kconfig.projbuild`).
- *Rationale:* Allows easy parameter tuning without recompiling code manually. Since Kconfig doesn't support floats natively, multiplier limits (`K_IDLE`, `K_DISTURBED`) will be configured as scaled integers (e.g., input 150 = 1.5x) and cast back to float in C++.

**3. O(1) Live Variance & Transitions:**
- *Decision:* Instead of batch variance calculations, maintain running sums (Welford's algorithm or Sum/SumSq) in `PushSample()`.
- *Rationale:* O(1) complexity per sample ensures deterministic execution time and prevents latency spikes in the sampling thread.

**4. Transition Triggers & Refresh:**
- *Decision:* 
  - `IDLE` -> `DISTURBED`: Triggered when short buffer variance exceeds previous 5-minute variance by `K_IDLE`. Short buffer is `std::copy`'d to the main buffer.
  - `DISTURBED` -> `IDLE`: Triggered when short variance falls below `K_DISTURBED`.
  - `DISTURBED` -> `DISTURBED` (Refresh): Triggered when buffer is `N_DPAD` samples away from full.
- *Rationale:* Double trigger ensures parameters are calculated (and immediately sent) at the end of a disturbance *and* prevents buffer overflow on prolonged disturbances.

## Risks / Trade-offs

- **Risk:** Scaling Kconfig integers to floats can result in precision loss.
  - **Mitigation:** Use sufficient scaling factors (e.g., x1000) for high-fidelity ratio limits.
- **Trade-off:** Maintaining running sums for variance requires more variables in the class state.
  - **Mitigation:** Adds minimal static RAM cost compared to the size of the 5-minute buffer.
