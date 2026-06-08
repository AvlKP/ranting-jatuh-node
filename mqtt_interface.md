# MQTT Interface Definition

This document defines the MQTT interface used for communication between the IoT nodes and the web application.

## Broker Configuration
- **Default URI**: `mqtt://broker.hivemq.com`
- **Protocol**: MQTT v5.0
- **Client ID Prefix**: `ranting-logger`

## Topics and Payloads

### 1. Monitoring Parameters
Node publishes processed monitoring data upon state transitions or buffer refreshes. Deployment mode batches parameter records to save power; debug mode may publish per event for faster feedback.

- **Topic**: `ranting/{node_id}/parameters`
- **Content Type**: `application/json`
- **Publish Frequency**: Batched for deployment, per event for debugging. Events include `IDLE` window update, `DISTURBED` buffer refresh, and final `DISTURBED`->`IDLE` result.
- **Payload**: one JSON object per record. Batched publishes may contain multiple newline-delimited JSON objects.
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
| `roll_sway_pp_max` | `float` | Max peak-to-peak roll sway in degrees. |
| `roll_sway_pp_mean` | `float` | Mean peak-to-peak roll sway in degrees. |
| `pitch_sway_pp_max` | `float` | Max peak-to-peak pitch sway in degrees. |
| `pitch_sway_pp_mean` | `float` | Mean peak-to-peak pitch sway in degrees. |
| `roll_damping_ratio` | `float` | Estimated damping ratio for roll. 0.0 for IDLE or intermediate DISTURBED refresh. |
| `pitch_damping_ratio` | `float` | Estimated damping ratio for pitch. 0.0 for IDLE or intermediate DISTURBED refresh. |
| `natural_freq_hz` | `float` | Estimated natural frequency in Hz (max of roll/pitch). |
| `natural_freq_roll_hz` | `float` | Estimated natural frequency for roll in Hz. 0.0 for IDLE or intermediate DISTURBED refresh. |
| `natural_freq_pitch_hz` | `float` | Estimated natural frequency for pitch in Hz. 0.0 for IDLE or intermediate DISTURBED refresh. |
| `state` | `string` | FSM state that produced this payload: `"IDLE"` or `"DISTURBED"`. |
| `sample_count` | `uint32` | Number of samples in the calculation window. |

### 2. Failure Events
Node publishes immediate notifications when a failure event is detected.

- **Topic**: `ranting/{node_id}/failures`
- **Content Type**: `text/csv`
- **Publish Frequency**: Immediate upon detection.
- **CSV Format**:
  `unix_time,timestamp_us,event_name`

| Column | Type | Description |
|--------|------|-------------|
| `unix_time` | `int64` | UTC Epoch time in seconds. 0 if time not synced. |
| `timestamp_us` | `uint64` | Monotonic system time in microseconds since boot. |
| `event_name` | `string` | Event type: `free_fall`, `acoustic_emission`, or `unknown`. |

## Implementation Notes
- **QoS**: Default 0 (at most once).
- **Time Sync**: Nodes attempt NTP sync before publishing. If sync fails, `unix_time` will be 0.
- **Node ID**: `{node_id}` comes from `CONFIG_LOGGER_NODE_ID`, stored NVS value, or generated adjective-noun ID on first boot.
- **Batching**: Deployment parameter publishes are batched and sent in one connection session to save power. Debug builds may publish per event. Failures trigger immediate publish.
