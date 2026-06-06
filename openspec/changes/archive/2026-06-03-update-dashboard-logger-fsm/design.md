## Context

The `monitor-fsm-fft-redesign` change introduced new monitor outputs (`natural_freq_roll_hz`, `natural_freq_pitch_hz`, and `state`). The MQTT JSON payload was updated to transmit these fields. However, two downstream consumers of this data on the ESP32-S3 itself were overlooked:
1. **Local SD Card Logging (`logger_storage.cpp`)**: Still writes a CSV header without the new fields, but the CSV lines constructed in `logger.cpp` were writing JSON objects instead of CSV rows. Wait, `logger.cpp` constructs a JSON payload in `FormatParameterCsv` and passes it to `AppendParameter`. This means the CSV file on the SD card is currently filled with a CSV header followed by JSON Lines.
2. **Local Debugging Dashboard (`dashboard.cpp`)**: The web dashboard does not parse or display `state`, `natural_freq_roll_hz`, or `natural_freq_pitch_hz`.

## Goals / Non-Goals

**Goals:**
- Fix the logger storage to correctly format CSV lines on the SD card (or align the header to state it's JSON if that's the intent, but the `.csv` extension implies CSV). We will align it to CSV format.
- Update the CSV header in `logger_storage.cpp` to include the new fields.
- Update the dashboard HTML/JS payload in `dashboard.cpp` to correctly visualize the new FSM state (`FREE_DECAY`) and per-axis frequency values.

**Non-Goals:**
- Changes to the core monitor FSM logic.
- Changes to the MQTT output format (it is already correct JSON).
- Changes to server-side ingestion scripts.

## Decisions

### Decision 1: CSV format for SD card logging

**Choice**: Modify `FormatParameterCsv` in `logger.cpp` to output actual comma-separated values instead of JSON. 
**Alternatives considered**: 
- Keep writing JSON but rename the file to `.jsonl`. This breaks existing parsing scripts that might expect `.csv`.
**Rationale**: The file extension is `.csv` and there's a CSV header. Writing JSON into it was likely a bug introduced during the MQTT JSON adaptation. It must be a proper CSV string matching the header.

### Decision 2: CSV header fields

**Choice**: Add `natural_freq_roll_hz`, `natural_freq_pitch_hz`, and `state` to the end of the CSV header in `logger_storage.cpp`.
**Rationale**: Preserves ordering of existing fields.

### Decision 3: Dashboard Visualization

**Choice**: Add a `FREE_DECAY` state badge alongside `IDLE` and `DISTURBED`. Split the natural frequency display into "Natural Frequency (Roll)" and "Natural Frequency (Pitch)" in the dashboard's HTML payload.
**Rationale**: Directly fulfills the new capabilities of the monitor while keeping the UI pattern consistent.

## Risks / Trade-offs

**[Risk] Fixing the CSV format breaks backward compatibility with tools that started parsing the JSON lines from the SD card.** → Mitigation: The system is in active development and the bug (JSON inside a CSV) is recent. Fixing it to be actual CSV is the correct path for a `.csv` file.

