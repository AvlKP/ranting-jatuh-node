import math
import numpy as np


def _select_peak_bin(freqs, power, freq_min_hz, freq_max_hz):
    if freqs is None or power is None or len(freqs) == 0 or len(power) == 0:
        return 0

    lower = max(float(freq_min_hz), 0.0)
    upper = float(freq_max_hz) if freq_max_hz is not None else float(freqs[-1])
    upper = min(upper, float(freqs[-1]))

    if upper < lower:
        return 0

    mask = (freqs >= lower) & (freqs <= upper)
    mask[0] = False
    bins = np.nonzero(mask)[0]
    if len(bins) == 0:
        return 0

    return int(bins[int(np.argmax(power[bins]))])


def compute_natural_frequency(signal, sample_rate_hz,
                              freq_min_hz=0.5,
                              freq_max_hz=12.0):
    n = len(signal)
    if n < 2:
        return 0.0, None, None

    min_fft = 512
    fft_window = 1024
    overlap = 512
    step = fft_window - overlap

    if n < fft_window:
        fft_size = min_fft if n < min_fft else fft_window

        padded = np.zeros(fft_size, dtype=np.float64)
        padded[:n] = signal

        mean = np.mean(padded[:n])
        padded[:n] -= mean

        window = 0.5 - 0.5 * np.cos(2.0 * math.pi * np.arange(n) / (n - 1))
        padded[:n] *= window

        fft = np.fft.rfft(padded)
        power = np.abs(fft) ** 2
        freqs = np.fft.rfftfreq(fft_size, 1.0 / sample_rate_hz)

        max_bin = _select_peak_bin(freqs, power, freq_min_hz, freq_max_hz)
        return freqs[max_bin], freqs, power

    segments = (n - fft_window) // step + 1
    span = fft_window + (segments - 1) * step
    base_start = n - span

    psd_accum = np.zeros(fft_window // 2 + 1, dtype=np.float64)

    for seg in range(segments):
        seg_start = base_start + seg * step
        seg_data = signal[seg_start:seg_start + fft_window]

        mean = np.mean(seg_data)
        seg_data = seg_data - mean

        window = 0.5 - 0.5 * np.cos(2.0 * math.pi *
                                      np.arange(fft_window) /
                                      (fft_window - 1))
        seg_data = seg_data * window

        fft = np.fft.rfft(seg_data)
        psd_accum += np.abs(fft) ** 2

    psd_accum /= segments
    freqs = np.fft.rfftfreq(fft_window, 1.0 / sample_rate_hz)

    max_bin = _select_peak_bin(freqs, psd_accum, freq_min_hz, freq_max_hz)
    return freqs[max_bin], freqs, psd_accum


def compute_disturbance_frequencies(df, state_mask, sample_rate_hz,
                                     roll_col='roll_tared',
                                     pitch_col='pitch_tared',
                                     freq_min_hz=0.5,
                                     freq_max_hz=12.0):
    state = df['state'].to_numpy(dtype=np.uint8) if state_mask is None else state_mask.to_numpy(dtype=np.uint8)

    events = []
    in_event = False
    start_idx = 0

    for i in range(len(state)):
        if state[i] == 1 and not in_event:
            start_idx = i
            in_event = True
        elif state[i] == 0 and in_event:
            events.append((start_idx, i))
            in_event = False
    if in_event:
        events.append((start_idx, len(state) - 1))

    results = []
    for idx, (start, end) in enumerate(events):
        if end - start < 20:
            continue

        roll_signal = df[roll_col].iloc[start:end].to_numpy(dtype=np.float64)
        pitch_signal = df[pitch_col].iloc[start:end].to_numpy(dtype=np.float64)

        freq_roll, freqs_r, psd_r = compute_natural_frequency(
            roll_signal, sample_rate_hz, freq_min_hz, freq_max_hz)
        freq_pitch, freqs_p, psd_p = compute_natural_frequency(
            pitch_signal, sample_rate_hz, freq_min_hz, freq_max_hz)

        results.append({
            'index': idx,
            'start': start,
            'end': end,
            't_start': float(df['timestamp_s'].iloc[start]),
            't_end': float(df['timestamp_s'].iloc[end]),
            'duration_s': float(df['timestamp_s'].iloc[end] - df['timestamp_s'].iloc[start]),
            'samples': end - start,
            'freq_roll_hz': freq_roll,
            'freq_pitch_hz': freq_pitch,
            'freqs_r': freqs_r,
            'psd_r': psd_r,
            'freqs_p': freqs_p,
            'psd_p': psd_p,
            'fft_search_min_hz': freq_min_hz,
            'fft_search_max_hz': freq_max_hz,
        })

    return results


def detect_extrema(data, sample_rate_hz, min_amp=0.1, min_spacing=2):
    n = len(data)
    extrema = []
    last_ext_idx = -1

    for i in range(1, n - 1):
        prev = data[i - 1]
        curr = data[i]
        nxt = data[i + 1]

        is_peak = (curr > prev) and (curr > nxt)
        is_trough = (curr < prev) and (curr < nxt)
        if not (is_peak or is_trough):
            continue

        if last_ext_idx >= 0 and (i - last_ext_idx) < min_spacing:
            continue

        extrema.append({
            'idx': i,
            'time': float(i) / sample_rate_hz,
            'value': float(curr),
            'kind': 1 if is_peak else -1,
        })
        last_ext_idx = i

    return extrema


def collapse_extrema_by_lobe(extrema, lobe_reversal_min_amp_deg):
    if not extrema:
        return []

    collapsed = []
    active_kind = None
    active = None

    for ext in extrema:
        if active_kind is None:
            active_kind = ext['kind']
            active = ext
        elif ext['kind'] == active_kind:
            if active_kind == 1:
                if ext['value'] > active['value']:
                    active = ext
            else:
                if ext['value'] < active['value']:
                    active = ext
        else:
            diff = abs(ext['value'] - active['value'])
            if diff >= lobe_reversal_min_amp_deg:
                collapsed.append(active)
                active_kind = ext['kind']
                active = ext

    if active is not None:
        collapsed.append(active)

    return collapsed


def compute_centerline_modal_signal(data, sample_rate_hz,
                                    min_amp=0.05,
                                    min_spacing=2,
                                    lobe_reversal_min_amp_deg=None):
    n = len(data)
    if n == 0:
        empty = np.array([], dtype=np.float64)
        return empty, empty, [], [], [], [], []

    if lobe_reversal_min_amp_deg is None:
        lobe_reversal_min_amp_deg = min_amp

    samples = np.asarray(data, dtype=np.float64)
    raw_extrema = detect_extrema(samples, sample_rate_hz, min_amp, min_spacing)
    collapsed_extrema = collapse_extrema_by_lobe(raw_extrema, lobe_reversal_min_amp_deg)

    center_indices = []
    center_values = []
    amp_times = []
    amplitudes = []
    pairs = []

    for prev, curr in zip(collapsed_extrema, collapsed_extrema[1:]):
        if prev['kind'] == curr['kind']:
            continue

        amp = abs(curr['value'] - prev['value']) * 0.5
        if amp < min_amp:
            continue

        center_idx = (prev['idx'] + curr['idx']) * 0.5
        center_val = (prev['value'] + curr['value']) * 0.5
        center_time = center_idx / sample_rate_hz

        center_indices.append(center_idx)
        center_values.append(center_val)
        amp_times.append(center_time)
        amplitudes.append(amp)
        pairs.append({
            'time': center_time,
            'amplitude': amp,
            'center': center_val,
            'left_idx': prev['idx'],
            'right_idx': curr['idx'],
            'left_value': prev['value'],
            'right_value': curr['value'],
        })

    if len(center_indices) < 2:
        centerline = np.full(n, float(np.mean(samples)), dtype=np.float64)
        residual = samples - centerline
        return centerline, residual, [], [], raw_extrema, collapsed_extrema, pairs

    x = np.arange(n, dtype=np.float64)
    centerline = np.interp(x, center_indices, center_values)
    residual = samples - centerline
    return centerline, residual, amp_times, amplitudes, raw_extrema, collapsed_extrema, pairs


def find_decay_amplitudes(times, amplitudes):
    if len(amplitudes) == 0:
        return []

    max_idx = int(np.argmax(amplitudes))
    return list(zip(times[max_idx:], amplitudes[max_idx:]))


def compute_sway(data, min_amp=0.1, min_spacing=2):
    n = len(data)
    if n < 3:
        return 0.0, 0.0, 0, 0.0, 0.0

    last_ext_idx = 0
    last_ext_val = 0.0
    has_last_ext = False
    max_pp = 0.0
    sum_pp = 0.0
    pp_count = 0
    max_ext = 0.0
    min_ext = 0.0

    for i in range(1, n - 1):
        prev = data[i - 1]
        curr = data[i]
        nxt = data[i + 1]

        is_peak = (curr > prev) and (curr > nxt) and (abs(curr) >= min_amp)
        is_trough = (curr < prev) and (curr < nxt) and (abs(curr) >= min_amp)

        if not (is_peak or is_trough):
            continue

        if has_last_ext and (i - last_ext_idx) < min_spacing:
            continue

        ext_val = curr
        if has_last_ext:
            pp = abs(ext_val - last_ext_val)
            if pp > max_pp:
                max_pp = pp
            sum_pp += pp
            pp_count += 1

        if ext_val > max_ext:
            max_ext = ext_val
        if ext_val < min_ext:
            min_ext = ext_val

        last_ext_idx = i
        last_ext_val = ext_val
        has_last_ext = True

    sway_pp_mean = sum_pp / float(pp_count) if pp_count > 0 else 0.0
    sway_amplitude = (max_ext - min_ext) / 2.0

    return max_pp, sway_pp_mean, pp_count, max_ext, min_ext


def find_decay_peaks(data, sample_rate_hz, min_amp=0.1, min_spacing=2):
    n = len(data)
    peaks = []
    all_times = []
    all_amplitudes = []
    all_signs = []

    last_ext_idx = 0
    has_last_ext = False

    for i in range(1, n - 1):
        prev = data[i - 1]
        curr = data[i]
        nxt = data[i + 1]

        is_peak = (curr > prev) and (curr > nxt) and (abs(curr) >= min_amp)
        is_trough = (curr < prev) and (curr < nxt) and (abs(curr) >= min_amp)

        if not (is_peak or is_trough):
            continue

        if has_last_ext and (i - last_ext_idx) < min_spacing:
            continue

        t = float(i) / sample_rate_hz
        amp = abs(curr)
        all_times.append(t)
        all_amplitudes.append(amp)
        all_signs.append(1.0 if curr > 0 else -1.0)

        last_ext_idx = i
        has_last_ext = True

    if not all_amplitudes:
        return [], [], [], 0.0, 0.0

    max_idx = int(np.argmax(all_amplitudes))
    start_idx = max_idx

    peaks.append((all_times[max_idx], all_amplitudes[max_idx]))
    last_amp = all_amplitudes[max_idx]

    for i in range(max_idx + 1, len(all_amplitudes)):
        amp = all_amplitudes[i]
        if amp > last_amp:
            break
        peaks.append((all_times[i], amp))
        last_amp = amp

    return peaks, all_times, all_amplitudes, all_signs, start_idx


def compute_damping_ratio(peaks, natural_freq_hz):
    if natural_freq_hz <= 0.0:
        return 0.0

    n = len(peaks)
    if n < 4:
        return 0.0

    wn = 2.0 * math.pi * natural_freq_hz
    sum_t = 0.0
    sum_y = 0.0
    sum_ty = 0.0
    sum_t2 = 0.0

    for t, amp in peaks:
        y = math.log(amp)
        sum_t += t
        sum_y += y
        sum_ty += t * y
        sum_t2 += t * t

    denominator = (n * sum_t2) - (sum_t * sum_t)
    if denominator <= 0.0:
        return 0.0

    slope = ((n * sum_ty) - (sum_t * sum_y)) / denominator
    if slope >= 0.0:
        return 0.0

    return abs(slope) / wn


def compute_disturbance_metrics(df, state_mask, sample_rate_hz,
                                 roll_col='roll_tared',
                                 pitch_col='pitch_tared',
                                 min_amp=0.1,
                                 centerline_min_amp=0.05,
                                 min_spacing=2,
                                 freq_min_hz=0.5,
                                 freq_max_hz=12.0,
                                 baseline_mode='centerline',
                                 lobe_reversal_min_amp_deg=None):
    state = df['state'].to_numpy(dtype=np.uint8) if state_mask is None else state_mask.to_numpy(dtype=np.uint8)

    events = []
    in_event = False
    start_idx = 0

    for i in range(len(state)):
        if state[i] == 1 and not in_event:
            start_idx = i
            in_event = True
        elif state[i] == 0 and in_event:
            events.append((start_idx, i))
            in_event = False
    if in_event:
        events.append((start_idx, len(state) - 1))

    results = []
    for idx, (start, end) in enumerate(events):
        if end - start < 20:
            continue

        roll_signal = df[roll_col].iloc[start:end].to_numpy(dtype=np.float64)
        pitch_signal = df[pitch_col].iloc[start:end].to_numpy(dtype=np.float64)

        freq_roll, freqs_r, psd_r = compute_natural_frequency(
            roll_signal, sample_rate_hz, freq_min_hz, freq_max_hz)
        freq_pitch, freqs_p, psd_p = compute_natural_frequency(
            pitch_signal, sample_rate_hz, freq_min_hz, freq_max_hz)

        r_pp_max, r_pp_mean, r_pp_count, r_max, r_min = compute_sway(
            roll_signal, min_amp, min_spacing)
        p_pp_max, p_pp_mean, p_pp_count, p_max, p_min = compute_sway(
            pitch_signal, min_amp, min_spacing)

        r_peaks, r_times, r_amps, r_signs, r_decay_start = find_decay_peaks(
            roll_signal, sample_rate_hz, min_amp, min_spacing)
        p_peaks, p_times, p_amps, p_signs, p_decay_start = find_decay_peaks(
            pitch_signal, sample_rate_hz, min_amp, min_spacing)

        zeta_roll = compute_damping_ratio(r_peaks, freq_roll)
        zeta_pitch = compute_damping_ratio(p_peaks, freq_pitch)

        r_centerline, r_residual, r_amp_times, r_pair_amps, r_raw_extrema, r_col_extrema, r_pairs = (
            compute_centerline_modal_signal(
                roll_signal, sample_rate_hz, centerline_min_amp, min_spacing,
                lobe_reversal_min_amp_deg))
        p_centerline, p_residual, p_amp_times, p_pair_amps, p_raw_extrema, p_col_extrema, p_pairs = (
            compute_centerline_modal_signal(
                pitch_signal, sample_rate_hz, centerline_min_amp, min_spacing,
                lobe_reversal_min_amp_deg))

        centerline_freq_roll, centerline_freqs_r, centerline_psd_r = (
            compute_natural_frequency(
                r_residual, sample_rate_hz, freq_min_hz, freq_max_hz))
        centerline_freq_pitch, centerline_freqs_p, centerline_psd_p = (
            compute_natural_frequency(
                p_residual, sample_rate_hz, freq_min_hz, freq_max_hz))

        r_decay_pairs = find_decay_amplitudes(r_amp_times, r_pair_amps)
        p_decay_pairs = find_decay_amplitudes(p_amp_times, p_pair_amps)
        centerline_zeta_roll = compute_damping_ratio(
            r_decay_pairs, centerline_freq_roll)
        centerline_zeta_pitch = compute_damping_ratio(
            p_decay_pairs, centerline_freq_pitch)

        selected_freq_roll = centerline_freq_roll if baseline_mode == 'centerline' else freq_roll
        selected_freq_pitch = centerline_freq_pitch if baseline_mode == 'centerline' else freq_pitch
        selected_zeta_roll = centerline_zeta_roll if baseline_mode == 'centerline' else zeta_roll
        selected_zeta_pitch = centerline_zeta_pitch if baseline_mode == 'centerline' else zeta_pitch

        results.append({
            'index': idx,
            'start': start,
            'end': end,
            't_start': float(df['timestamp_s'].iloc[start]),
            't_end': float(df['timestamp_s'].iloc[end]),
            'duration_s': float(df['timestamp_s'].iloc[end] - df['timestamp_s'].iloc[start]),
            'samples': end - start,
            'freq_roll_hz': selected_freq_roll,
            'freq_pitch_hz': selected_freq_pitch,
            'legacy_freq_roll_hz': freq_roll,
            'legacy_freq_pitch_hz': freq_pitch,
            'freqs_r': freqs_r,
            'psd_r': psd_r,
            'freqs_p': freqs_p,
            'psd_p': psd_p,
            'fft_search_min_hz': freq_min_hz,
            'fft_search_max_hz': freq_max_hz,
            'centerline_min_amp_deg': centerline_min_amp,
            'sway_roll_pp_max': r_pp_max,
            'sway_roll_pp_mean': r_pp_mean,
            'sway_pitch_pp_max': p_pp_max,
            'sway_pitch_pp_mean': p_pp_mean,
            'roll_max_ext_deg': r_max,
            'roll_min_ext_deg': r_min,
            'pitch_max_ext_deg': p_max,
            'pitch_min_ext_deg': p_min,
            'zeta_roll': selected_zeta_roll,
            'zeta_pitch': selected_zeta_pitch,
            'legacy_zeta_roll': zeta_roll,
            'legacy_zeta_pitch': zeta_pitch,
            'r_peaks': r_peaks,
            'p_peaks': p_peaks,
            'r_all_times': r_times,
            'r_all_amps': r_amps,
            'r_signs': r_signs,
            'p_all_times': p_times,
            'p_all_amps': p_amps,
            'p_signs': p_signs,
            'centerline_freq_roll_hz': centerline_freq_roll,
            'centerline_freq_pitch_hz': centerline_freq_pitch,
            'centerline_zeta_roll': centerline_zeta_roll,
            'centerline_zeta_pitch': centerline_zeta_pitch,
            'roll_centerline': r_centerline,
            'pitch_centerline': p_centerline,
            'roll_residual': r_residual,
            'pitch_residual': p_residual,
            'roll_decay_amplitudes': r_decay_pairs,
            'pitch_decay_amplitudes': p_decay_pairs,
            'roll_centerline_pairs': r_pairs,
            'pitch_centerline_pairs': p_pairs,
            'roll_extrema': r_raw_extrema,
            'pitch_extrema': p_raw_extrema,
            'roll_collapsed_extrema': r_col_extrema,
            'pitch_collapsed_extrema': p_col_extrema,
            'lobe_reversal_min_amp_deg': (lobe_reversal_min_amp_deg
                                           if lobe_reversal_min_amp_deg is not None
                                           else centerline_min_amp),
            'centerline_freqs_r': centerline_freqs_r,
            'centerline_psd_r': centerline_psd_r,
            'centerline_freqs_p': centerline_freqs_p,
            'centerline_psd_p': centerline_psd_p,
        })

    return results
