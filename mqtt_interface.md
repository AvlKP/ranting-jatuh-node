# MQTT Interface Definition

This document defines the MQTT interface used for communication between the IoT nodes and the web application.

## Broker Configuration
- **Default URI**: `mqtt://broker.hivemq.com`
- **Protocol**: MQTT v5.0
- **Client ID Prefix**: `ranting-logger`

## Node Identification

Each node has a unique **Node ID** used as a topic prefix to distinguish data sources in multi-node deployments.

- **Format**: `{adjective}-{noun}` (e.g., `quiet-pine`, `bold-oak`) — auto-generated on first boot
- **Persistence**: Stored in NVS, survives reboots. Erase NVS to regenerate.
- **Override**: Set `CONFIG_LOGGER_NODE_ID` in Kconfig to use a known provisioning ID
- **Visibility**: Available in dashboard `/api/status` response as `node_id` field

## FSM Overview

The node runs a two-state finite state machine that governs when parameter analysis and publishing occur.

```
                  accel_err_var > threshold
  ┌──────┐  ───────────────────────────────>  ┌───────────┐
  │ IDLE │                                     │ DISTURBED │
  └──────┘  <───────────────────────────────  └───────────┘
                  accel_err_var < threshold
                  (debounced)
```

### States

| State | Description | Publishing |
|-------|-------------|------------|
| `IDLE` | Sensor stationary. Tilt baseline (mean, variance) and accel error variance baseline computed per 5-minute window. | No parameters published. |
| `DISTURBED` | Motion detected — accel error variance exceeds adaptive threshold. Samples accumulated in 5-minute ring buffer. | Parameters published on buffer refresh (intermediate sway stats) and on exit transition (full post-hoc decay analysis). |

### Transitions

- **IDLE → DISTURBED**: When short-buffer accel error variance exceeds `max(baseline_var × K_HIGH, K_ABS_MIN_ACCEL_VAR)`. Immediate, no debounce.
- **DISTURBED → IDLE**: When short-buffer accel error variance drops below `max(baseline_var × K_LOW, K_ABS_MIN_ACCEL_VAR)` for `N` consecutive samples. On exit, post-hoc decay analysis runs on the stored DISTURBED buffer.

### Publish Triggers

| Trigger | State in Payload | What is Computed |
|---------|------------------|------------------|
| DISTURBED buffer near-full (refresh) | `"DISTURBED"` | Sway (peak-to-peak max/mean). Natural frequency and damping ratio set to 0.0. |
| DISTURBED → IDLE (exit) | `"DISTURBED"` | Sway + post-hoc decay analysis: peak envelope, damping ratio via log-linear regression, natural frequency via Welch FFT. |

### Key Fields for Server Interpretation

- **`state`**: Always `"DISTURBED"` when parameters are received — the node does not publish from IDLE. The field preserves the source state for the analysis.
- **`natural_freq_hz` / `damping_ratio`**: Zero on buffer refresh; non-zero only on DISTURBED exit (decay analysis complete).
- **`sample_count`**: Number of samples in the analysis window. Buffer refresh: ~5-minute worth. Exit: up to full buffer.

## Topics and Payloads

All topics follow the format `ranting/{node_id}/{datatype}` where `{node_id}` is the per-node identifier.

### 1. Monitoring Parameters
Node publishes processed monitoring data event-driven upon state transitions or buffer refreshes.

- **Topic**: `ranting/{node_id}/parameters`
- **Server Wildcard**: `ranting/+/parameters` (captures all nodes)
- **Content Type**: `application/json`
- **Publish Frequency**:
  - On DISTURBED buffer refresh (intermediate — every ~5 minutes during prolonged disturbance)
  - On DISTURBED → IDLE transition (exit analysis — immediately after disturbance subsides)
- **JSON Payload Schema**:

```json
{
  "unix_time": 1672531200,
  "timestamp_us": 12345678,
  "roll_mean": 0.05,
  "pitch_mean": -0.02,
  "roll_variance": 0.001,
  "pitch_variance": 0.001,
  "roll_sway_pp_max": 2.5,
  "roll_sway_pp_mean": 1.2,
  "pitch_sway_pp_max": 1.8,
  "pitch_sway_pp_mean": 0.9,
  "roll_damping_ratio": 0.045,
  "pitch_damping_ratio": 0.038,
  "natural_freq_hz": 4.15,
  "natural_freq_roll_hz": 4.15,
  "natural_freq_pitch_hz": 3.85,
  "state": "DISTURBED",
  "sample_count": 512
}
```

| Field | Type | Description |
|-------|------|-------------|
| `unix_time` | `int64` | UTC Epoch time in seconds. 0 if time not synced. |
| `timestamp_us` | `uint64` | Monotonic system time in microseconds since boot. |
| `roll_mean` | `float` | Mean roll angle in degrees. |
| `pitch_mean` | `float` | Mean pitch angle in degrees. |
| `roll_variance` | `float` | Variance of roll angle. |
| `pitch_variance` | `float` | Variance of pitch angle. |
| `roll_sway_pp_max` | `float` | Max peak-to-peak roll sway in degrees during disturbance. |
| `roll_sway_pp_mean` | `float` | Mean peak-to-peak roll sway in degrees during disturbance. |
| `pitch_sway_pp_max` | `float` | Max peak-to-peak pitch sway in degrees during disturbance. |
| `pitch_sway_pp_mean` | `float` | Mean peak-to-peak pitch sway in degrees during disturbance. |
| `roll_damping_ratio` | `float` | Estimated damping ratio for roll via log-linear regression on decay peaks. 0.0 on buffer refresh (no decay analysis). |
| `pitch_damping_ratio` | `float` | Estimated damping ratio for pitch. 0.0 on buffer refresh. |
| `natural_freq_hz` | `float` | Estimated natural frequency in Hz (max of roll/pitch). 0.0 on buffer refresh. |
| `natural_freq_roll_hz` | `float` | Estimated natural frequency for roll in Hz via Welch FFT. 0.0 on buffer refresh. |
| `natural_freq_pitch_hz` | `float` | Estimated natural frequency for pitch in Hz via Welch FFT. 0.0 on buffer refresh. |
| `state` | `string` | Source FSM state: `"DISTURBED"` only (parameters are never published from IDLE). |
| `sample_count` | `uint32` | Number of samples in the analysis window. |

### 2. Failure Events
Node publishes immediate notifications when a failure event is detected.

- **Topic**: `ranting/{node_id}/failures`
- **Server Wildcard**: `ranting/+/failures`
- **Content Type**: `text/csv`
- **Publish Frequency**: Immediate upon detection.
- **CSV Format**:
  `unix_time,timestamp_us,event_name`

| Column | Type | Description |
|--------|------|-------------|
| `unix_time` | `int64` | UTC Epoch time in seconds. 0 if time not synced. |
| `timestamp_us` | `uint64` | Monotonic system time in microseconds since boot. |
| `event_name` | `string` | Event type: `free_fall`, `acoustic_emission`, or `unknown`. |

### 3. Verification
Startup self-test verification publish (only if `CONFIG_APP_VERIFY_ENABLE` is set).

- **Topic**: `ranting/{node_id}/verify`
- **Server Wildcard**: `ranting/+/verify`
- **Payload**: `verify_ok` (text/plain)

## Implementation Notes
- **QoS**: Default 0 (at most once).
- **Time Sync**: Nodes attempt NTP sync before publishing. If sync fails, `unix_time` will be 0.
- **Batching**: Parameters are batched and sent in a single connection session to save power. Failures trigger an immediate connection.
- **Multi-Node**: Server should subscribe with wildcard patterns (`ranting/+/parameters`, `ranting/+/failures`, `ranting/+/verify`) and extract `{node_id}` from the topic to identify the source node.
- **No parameters from IDLE**: The node only publishes parameters when leaving DISTURBED or during a DISTURBED buffer refresh. No regular heartbeat from IDLE.
- **Decay completeness**: Check `natural_freq_hz > 0` or `roll_damping_ratio != 0` to distinguish exit analysis from buffer refresh. Zero-values indicate incomplete decay analysis.
