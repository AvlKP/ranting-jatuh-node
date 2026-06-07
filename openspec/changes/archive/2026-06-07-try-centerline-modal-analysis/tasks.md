## 1. OpenSpec Artifacts

- [x] 1.1 Create proposal, design, spec, and task artifacts for the notebook experiment

## 2. Analysis Implementation

- [x] 2.1 Change notebook HPF default cutoff to 0.2 Hz
- [x] 2.2 Add bounded FFT search parameters and output diagnostics
- [x] 2.3 Add centerline detrending and half peak-to-peak damping helpers
- [x] 2.4 Integrate centerline mode into disturbance metrics while preserving legacy metrics

## 3. Notebook and Documentation

- [x] 3.1 Update analysis notebook workflow to use HPF 0.2 Hz and centerline modal metrics
- [x] 3.2 Record design decision in notebook notes

## 4. Verification

- [x] 4.1 Run analysis helpers on logs/raw_log_7.csv
- [x] 4.2 Verify event near 96 s is detected and FFT search is limited to 0.5-12 Hz
