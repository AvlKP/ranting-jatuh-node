# MQTT Interface Definition

This document defines the MQTT interface used for communication between the IoT nodes and the web application.

## Broker Configuration
- **Default URI**: `mqtt://broker.hivemq.com`
- **Protocol**: MQTT v5.0
- **Client ID Prefix**: `ranting-logger`

## Topics and Payloads

### 1. Monitoring Parameters
Node publishes batches of processed monitoring data periodically.

- **Topic**: `ranting/parameters`
- **Content Type**: `text/csv`
- **Publish Frequency**: Default every 6 hours (configurable via `LOGGER_WIFI_PERIOD_HOURS`).
- **CSV Format**:
  `unix_time,timestamp_us,roll_mean,pitch_mean,roll_variance,pitch_variance,roll_sway_pp_max,roll_sway_pp_mean,pitch_sway_pp_max,pitch_sway_pp_mean,roll_damping_ratio,pitch_damping_ratio,natural_freq_hz,sample_count`

| Column | Type | Description |
|--------|------|-------------|
| `unix_time` | `int64` | UTC Epoch time in seconds. 0 if time not synced. |
| `timestamp_us` | `uint64` | Monotonic system time in microseconds since boot. |
| `roll_mean` | `float` | Mean roll angle in degrees. |
| `pitch_mean` | `float` | Mean pitch angle in degrees. |
| `roll_variance` | `float` | Variance of roll angle. |
| `pitch_variance` | `float` | Variance of pitch angle. |
| `roll_sway_pp_max` | `float` | Max peak-to-peak roll sway. |
| `roll_sway_pp_mean` | `float` | Mean peak-to-peak roll sway. |
| `pitch_sway_pp_max` | `float` | Max peak-to-peak pitch sway. |
| `pitch_sway_pp_mean` | `float` | Mean peak-to-peak pitch sway. |
| `roll_damping_ratio` | `float` | Estimated damping ratio for roll. |
| `pitch_damping_ratio` | `float` | Estimated damping ratio for pitch. |
| `natural_freq_hz` | `float` | Estimated natural frequency in Hz. |
| `sample_count` | `uint32` | Number of samples in the window. |

### 2. Failure Events
Node publishes immediate notifications when a failure event is detected.

- **Topic**: `ranting/failures`
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
- **Batching**: Parameters are batched and sent in a single connection session to save power. Failures trigger an immediate connection.
