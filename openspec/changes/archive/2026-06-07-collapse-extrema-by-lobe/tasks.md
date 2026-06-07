## 1. Extrema Collapse Helpers

- [x] 1.1 Add a lobe-collapse helper in `notebook/natural_frequency.py` that accepts raw extrema and returns one strongest extrema per confirmed peak/trough lobe
- [x] 1.2 Add configurable `lobe_reversal_min_amp_deg`, defaulting to `centerline_min_amp` when omitted
- [x] 1.3 Preserve raw extrema output while adding collapsed extrema output for diagnostics

## 2. Centerline and Damping Integration

- [x] 2.1 Build centerline peak/trough pairs from collapsed extrema instead of raw extrema
- [x] 2.2 Compute half peak-to-peak damping amplitudes from collapsed extrema pairs
- [x] 2.3 Preserve existing fallback behavior when collapsed extrema produce too few valid pairs

## 3. Notebook Diagnostics

- [x] 3.1 Update `notebook/analysis.ipynb` baseline compensation visualization to show raw extrema, collapsed extrema, accepted pair links, and damping amplitudes
- [x] 3.2 Add summary output for raw extrema count, collapsed extrema count, pair count, and damping amplitude count per axis
- [x] 3.3 Update `notebook/NOTES.md` if tuning or observed behavior changes

## 4. Verification

- [x] 4.1 Run notebook helpers on `logs/raw_log_7.csv`
- [x] 4.2 Verify multiple extrema inside one peak/trough lobe collapse to one representative extrema
- [x] 4.3 Verify event near 96 s still reports bounded FFT results and non-crashing damping output
