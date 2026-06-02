## 1. Refactor ComputeNaturalFrequency

- [x] 1.1 Replace the early `count < kFftWindowSamples` guard: keep only `count == 0` as the bail-out that zeroes `psd_accum_` and returns false.
- [x] 1.2 Add FFT size selection logic: `count < 512` → `fft_size = 512`, `512 ≤ count < 1024` → `fft_size = 1024`, `count ≥ 1024` → `fft_size = 1024` (Welch path).
- [x] 1.3 Implement single-window zero-padded FFT path: copy `count` samples from history with Hann window, zero-fill remaining slots up to `fft_size`, run `dsps_fft2r_fc32` + `dsps_bit_rev_fc32` with `fft_size`.
- [x] 1.4 Implement 512-pt bin mapping: write each of 256 PSD bins into two consecutive `psd_accum_` slots (`psd_accum_[2*i] = psd_accum_[2*i+1] = power`).
- [x] 1.5 For 1024-pt single-window path, write 512 PSD bins directly to `psd_accum_` (same layout as Welch, just one segment).
- [x] 1.6 Compute `natural_freq_hz` using actual `fft_size` in the denominator: `(max_bin * sample_rate) / fft_size`.

## 2. Verify

- [x] 2.1 Build firmware and confirm no compilation errors.
- [ ] 2.2 Test short disturbance (< 512 samples): confirm FFT graph updates on DISTURBED→IDLE transition instead of showing -120 dB.
- [ ] 2.3 Test medium disturbance (512–1023 samples): confirm FFT graph updates.
- [ ] 2.4 Test long disturbance (≥ 1024 samples): confirm Welch behavior unchanged.
