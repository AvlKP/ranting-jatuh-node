## REMOVED Requirements

### Requirement: CSV Debug Logging Configuration
**Reason**: Debug CSV logging feature is being removed entirely. The configuration option introduced unnecessary complexity in the logger initialization path.
**Migration**: No replacement. Raw acceleration and tilt data can be observed through the HTTP dashboard at runtime.

### Requirement: CSV Debug Log Formatting
**Reason**: The CSV formatting and ring-buffer delivery from monitor to logger added coupling between components at IMU sample rate (26–208 Hz), degrading logger task responsiveness.
**Migration**: No replacement.

### Requirement: CSV Log File Overwrite
**Reason**: Removed with the debug CSV logging feature.
**Migration**: No replacement.

### Requirement: Exclude Debug Logs from Server Transmission
**Reason**: No debug logs exist to exclude.
**Migration**: No action needed.

### Requirement: State Column in StreamSample
**Reason**: The StreamSample struct and associated event posting path are removed.
**Migration**: No replacement.

### Requirement: Batched CSV Flush
**Reason**: The ring buffer and 1 Hz flush timer are removed.
**Migration**: No replacement.
