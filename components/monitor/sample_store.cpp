/// @file sample_store.cpp
/// @brief Rolling sample buffers and dashboard snapshot APIs.
/// @details Owns the long history buffer, short pre-trigger buffer, and live stream
/// sample ring. All public snapshot methods copy data under mutex_ and return before
/// any I/O or slow work. The monitor task is the exclusive writer.
/// @ingroup monitor

#include "monitor.hpp"
#include "monitor_internal.hpp"

#include <algorithm>

#include "esp_log.h"

namespace monitor {

void Monitor::PushSample(float roll, float pitch,
                         float gx, float gy, float gz,
                         float ax, float ay, float az,
                         float gmag, float tkeo) noexcept {
    bool should_compute = false;
    bool should_reset_after_compute = false;
    NodeState pub_state = NodeState::IDLE;
    bool is_exit = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        const NodeState old_detector_state = dsp_detector_.State();
        const NodeState new_detector_state = dsp_detector_.Update(gmag, tkeo, config_);

        auto write_history = [this](std::size_t idx,
                                    float r, float p,
                                    float gx_v, float gy_v, float gz_v,
                                    float ax_v, float ay_v, float az_v,
                                    float gm) {
            roll_history_[idx] = r;
            pitch_history_[idx] = p;
            if (idx < kEventSamples) {
                gx_history_[idx] = gx_v;
                gy_history_[idx] = gy_v;
                gz_history_[idx] = gz_v;
                ax_history_[idx] = ax_v;
                ay_history_[idx] = ay_v;
                az_history_[idx] = az_v;
            }
            gmag_history_[idx] = gm;
        };

        auto append_history = [&write_history, this](float r, float p,
                                                     float gx_v, float gy_v, float gz_v,
                                                     float ax_v, float ay_v, float az_v,
                                                     float gm) {
            write_history(write_index_, r, p, gx_v, gy_v, gz_v, ax_v, ay_v, az_v, gm);
            write_index_ = (write_index_ + 1U) % kStorageSamples;
            if (sample_count_ < kStorageSamples) {
                ++sample_count_;
            }
        };

        auto reset_from_short = [&write_history, this]() {
            write_index_ = 0U;
            sample_count_ = 0U;
            const std::size_t count = short_sample_count_;
            for (std::size_t i = 0U; i < count; ++i) {
                const std::size_t idx = (short_write_index_ + kShortBufferSamples - count + i) % kShortBufferSamples;
                write_history(i,
                              roll_short_[idx], pitch_short_[idx],
                              gx_short_[idx], gy_short_[idx], gz_short_[idx],
                              ax_short_[idx], ay_short_[idx], az_short_[idx],
                              gmag_short_[idx]);
            }
            write_index_ = count;
            sample_count_ = count;
        };

        if (short_sample_count_ == kShortBufferSamples) {
        } else {
            ++short_sample_count_;
        }

        roll_short_[short_write_index_] = roll;
        pitch_short_[short_write_index_] = pitch;
        gx_short_[short_write_index_] = gx;
        gy_short_[short_write_index_] = gy;
        gz_short_[short_write_index_] = gz;
        ax_short_[short_write_index_] = ax;
        ay_short_[short_write_index_] = ay;
        az_short_[short_write_index_] = az;
        gmag_short_[short_write_index_] = gmag;

        short_write_index_ = (short_write_index_ + 1U) % kShortBufferSamples;

        if (state_.load() == NodeState::IDLE) {
            append_history(roll, pitch, gx, gy, gz, ax, ay, az, gmag);

            if ((old_detector_state == NodeState::IDLE) && (new_detector_state == NodeState::DISTURBED)) {
                state_.store(NodeState::DISTURBED);
                reset_from_short();

                disturbed_exit_debounce_counter_ = 0U;
                peak_gmag_ = gmag;

                ESP_LOGI(kTag, "Transition: IDLE -> DISTURBED (gmag=%.6f tkeo=%.6f)",
                         static_cast<double>(gmag), static_cast<double>(tkeo));
            }
        } else if (state_.load() == NodeState::DISTURBED) {
            append_history(roll, pitch, gx, gy, gz, ax, ay, az, gmag);
            peak_gmag_ = std::max(peak_gmag_, gmag);

            bool transitioned = false;
            disturbed_exit_debounce_counter_ = dsp_detector_.QuietCount();
            if ((old_detector_state == NodeState::DISTURBED) && (new_detector_state == NodeState::IDLE)) {
                state_.store(NodeState::IDLE);
                transitioned = true;

                ESP_LOGI(kTag, "Transition: DISTURBED -> IDLE (gmag=%.6f tkeo=%.6f)",
                         static_cast<double>(gmag), static_cast<double>(tkeo));

                should_compute = true;
                pub_state = NodeState::DISTURBED;
                is_exit = true;
            }

            if (!transitioned && sample_count_ >= (kEventSamples - static_cast<std::size_t>(CONFIG_MONITOR_N_DPAD))) {
                ESP_LOGI(kTag, "DISTURBED Buffer Refreshed");

                should_compute = true;
                pub_state = NodeState::DISTURBED;
                is_exit = false;
                should_reset_after_compute = true;
            }
        }
    } // mutex_ released here

    if (should_compute) {
        static_cast<void>(ComputeAndPublish(pub_state, is_exit));
        if (is_exit) {
            std::lock_guard<std::mutex> lock(mutex_);
            peak_gmag_ = 0.0f;
        }
        if (should_reset_after_compute) {
            std::lock_guard<std::mutex> lock(mutex_);
            write_index_ = 0U;
            sample_count_ = 0U;
            const std::size_t count = short_sample_count_;
            for (std::size_t i = 0U; i < count; ++i) {
                const std::size_t idx = (short_write_index_ + kShortBufferSamples - count + i) % kShortBufferSamples;
                roll_history_[i] = roll_short_[idx];
                pitch_history_[i] = pitch_short_[idx];
                gx_history_[i] = gx_short_[idx];
                gy_history_[i] = gy_short_[idx];
                gz_history_[i] = gz_short_[idx];
                ax_history_[i] = ax_short_[idx];
                ay_history_[i] = ay_short_[idx];
                az_history_[i] = az_short_[idx];
                gmag_history_[i] = gmag_short_[idx];
            }
            write_index_ = count;
            sample_count_ = count;
        }
    }
}

std::size_t Monitor::BufferSize() const noexcept {
    return (sample_count_ < kStorageSamples) ? sample_count_ : kStorageSamples;
}

std::size_t Monitor::StartIndex() const noexcept {
    return (sample_count_ < kStorageSamples) ? 0U : write_index_;
}

std::size_t Monitor::PhysicalIndex(std::size_t logical_index) const noexcept {
    return (StartIndex() + logical_index) % kStorageSamples;
}

void Monitor::GetFftData(float* out_psd, std::size_t& out_len) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    out_len = kFftWindowSamples / 2U;
    if (out_psd != nullptr) {
        for (std::size_t i = 0U; i < out_len; ++i) {
            out_psd[i] = psd_accum_[i];
        }
    }
}

void Monitor::GetTiltHistory(float* out_roll, float* out_pitch, std::size_t& out_len, std::size_t max_len) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t count = BufferSize();
    out_len = (count < max_len) ? count : max_len;
    
    if (out_roll != nullptr && out_pitch != nullptr) {
        std::size_t start_idx = count - out_len;
        for (std::size_t i = 0U; i < out_len; ++i) {
            std::size_t idx = PhysicalIndex(start_idx + i);
            out_roll[i] = roll_history_[idx];
            out_pitch[i] = pitch_history_[idx];
        }
    }
}

void Monitor::GetLatestSamples(StreamSample* out_samples, std::size_t& out_len, std::size_t max_len) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    out_len = (stream_count_ < max_len) ? stream_count_ : max_len;
    if (out_samples != nullptr) {
        std::size_t start_idx = (stream_count_ < kMaxStreamSamples) ? 0U : stream_write_index_;
        for (std::size_t i = 0U; i < out_len; ++i) {
            std::size_t idx = (start_idx + i) % kMaxStreamSamples;
            out_samples[i] = stream_samples_[idx];
        }
    }
}

} // namespace monitor
