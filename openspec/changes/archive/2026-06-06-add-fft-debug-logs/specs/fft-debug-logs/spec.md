# fft-debug-logs Specification

## Purpose
Defines the CSV debug logging system for FFT power spectral density and peak envelope data, enabling offline analysis of natural frequency and damping ratio calculations.

## ADDED Requirements

### Requirement: FFT PSD CSV Logging
The system SHALL log per-bin power spectral density data to a CSV file when a DISTURBED-to-IDLE analysis event completes. The file SHALL be overwritten at startup and gated behind `CONFIG_APP_DEBUG_CSV_LOGS`.

#### Scenario: Analysis event produces FFT CSV rows
- **WHEN** the monitor completes `ComputeAndPublish()` with `is_exit=true` and both roll and pitch axis natural frequencies have been computed
- **THEN** the system SHALL format and write rows to `fft.csv`, one row per PSD bin per axis, with columns `timestamp_ms,axis,bin,freq_hz,psd_power`

#### Scenario: FFT CSV file is overwritten on boot
- **WHEN** the system initializes with CSV debug logging enabled
- **THEN** the previous contents of `fft.csv` SHALL be cleared

#### Scenario: FFT CSV excluded from server transmission
- **WHEN** the system processes logs for server transmission
- **THEN** the system SHALL NOT transmit FFT debug CSV data to the server

### Requirement: Peak Envelope CSV Logging
The system SHALL log detected peak and trough amplitudes from the decay region to a CSV file when a DISTURBED-to-IDLE analysis event completes. The file SHALL be overwritten at startup and gated behind `CONFIG_APP_DEBUG_CSV_LOGS`.

#### Scenario: Analysis event produces peak envelope CSV rows
- **WHEN** the monitor completes `ComputeAndPublish()` with `is_exit=true` and peak detection has been performed for both roll and pitch axes
- **THEN** the system SHALL format and write rows to `peaks.csv`, one row per detected peak/trough per axis, with columns `timestamp_ms,axis,peak_idx,time_s,amplitude_deg,log_amplitude`

#### Scenario: Peak CSV file is overwritten on boot
- **WHEN** the system initializes with CSV debug logging enabled
- **THEN** the previous contents of `peaks.csv` SHALL be cleared

#### Scenario: Peak CSV excluded from server transmission
- **WHEN** the system processes logs for server transmission
- **THEN** the system SHALL NOT transmit peak envelope debug CSV data to the server

### Requirement: FFT and Peak Data Capture in Monitor
The monitor SHALL capture intermediate FFT PSD data and peak envelope data into dedicated debug members during analysis, making them available to the logger for CSV formatting.

#### Scenario: PSD data captured during FFT computation
- **WHEN** `ComputeAxisNaturalFrequency()` computes the per-segment PSD during Welch FFT
- **THEN** the normalized PSD array SHALL be copied to a debug storage member accessible by the logger

#### Scenario: Peak data captured during decay region detection
- **WHEN** `FindDecayRegion()` identifies the declining peak envelope
- **THEN** the peak amplitudes, times, and count SHALL be copied to debug storage members accessible by the logger

### Requirement: Batched FFT and Peak CSV Flush
The system SHALL flush buffered FFT and peak CSV log lines using the same batched write strategy as the existing stream-sample debug CSV.

#### Scenario: Periodic flush of FFT and peak CSV buffers
- **WHEN** the logger task performs its 1-second debug flush check and buffered FFT or peak CSV lines exist
- **THEN** the system opens each file, writes all buffered lines in sequence, and closes each file

#### Scenario: Shutdown flush of FFT and peak CSV data
- **WHEN** the system is shutting down or the logger task is terminating
- **THEN** any remaining buffered FFT and peak CSV log lines SHALL be flushed to their respective files before the logger module finalizes
