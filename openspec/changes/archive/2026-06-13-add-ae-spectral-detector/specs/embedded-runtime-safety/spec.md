## ADDED Requirements

### Requirement: Spectral acoustic emission processing SHALL be bounded
High-rate acoustic emission ADC sampling and FFT processing SHALL use bounded task execution, persistent buffers, and observable backpressure.

#### Scenario: Spectral AE task hot path
- **WHEN** spectral acoustic emission mode is enabled and one 256-sample window is processed
- **THEN** the acquisition and FFT processing path SHALL avoid heap allocation
- **AND** it SHALL avoid file I/O and network I/O
- **AND** it SHALL use bounded loops over fixed-size buffers only

#### Scenario: Spectral AE buffers are not stack-heavy
- **WHEN** implementation code stores ADC windows, FFT input, FFT output, or gradient history for spectral acoustic emission processing
- **THEN** those buffers SHALL use bounded persistent storage or explicitly measured task stack storage
- **AND** verification SHALL record stack high-water margin for the AE processing task when the task exists

#### Scenario: Spectral AE event backpressure is visible
- **WHEN** spectral acoustic emission detection publishes failures faster than the ESP event loop can accept them
- **THEN** failed event posts SHALL be counted or logged without blocking the spectral processing task indefinitely
- **AND** diagnostics SHALL expose the dropped failure count through existing monitor failure drop reporting

#### Scenario: Spectral AE build records resource impact
- **WHEN** spectral acoustic emission mode is enabled for validation
- **THEN** verification SHALL record `idf.py size` output
- **AND** verification SHALL record boot logs showing critical tasks started
- **AND** verification SHALL include runtime evidence that monitor, logger, and AE processing tasks remain alive during spectral detection
