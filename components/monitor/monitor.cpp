#include "monitor.hpp"

#include <cmath>

#include "dsps_fft2r.h"
#include "esp_timer.h"

namespace monitor {

namespace {

constexpr float kTwoPi = 6.2831853071795864769f;

} // namespace

static_assert(kStorageSamples >= kFftWindowSamples,
              "Monitor storage window must be >= 1024 samples.");

Monitor::Monitor(const sensor::Lsm6ds3::Config& imu_config,
                 const MonitorConfig& config) noexcept
    : imu_{imu_config},
      filter_{config.filter_alpha},
      config_{config} {}

bool Monitor::Init() noexcept {
    if (!imu_.init()) {
        return false;
    }

    const esp_err_t err = dsps_fft2r_init_fc32(nullptr, static_cast<int>(kFftWindowSamples));
    if (err != ESP_OK) {
        return false;
    }

    fft_initialized_ = true;
    return true;
}

void Monitor::RegisterCallback(EventCb cb, void* ctx) noexcept {
    callback_ = cb;
    callback_ctx_ = ctx;
}

bool Monitor::Update(float dt_s) noexcept {
    sensor::lsm6ds3::Value gyro{};
    sensor::lsm6ds3::Value accel{};
    if (!ReadImu(gyro, accel)) {
        return false;
    }

    const std::array<float, 3> accel_vec{accel.x, accel.y, accel.z};
    const std::array<float, 3> gyro_vec{gyro.x, gyro.y, gyro.z};
    filter_.update(accel_vec, gyro_vec, dt_s);

    PushSample(filter_.roll(), filter_.pitch());

    if ((sample_count_ >= kStorageSamples) && (write_index_ == 0U)) {
        return ComputeAndPublish();
    }

    return true;
}

bool Monitor::ReadImu(sensor::lsm6ds3::Value& gyro,
                      sensor::lsm6ds3::Value& accel) noexcept {
    return imu_.read_accel_gyro(gyro, accel);
}

void Monitor::PushSample(float roll, float pitch) noexcept {
    roll_history_[write_index_] = roll;
    pitch_history_[write_index_] = pitch;

    write_index_ = (write_index_ + 1U) % kStorageSamples;
    if (sample_count_ < kStorageSamples) {
        ++sample_count_;
    }
}

bool Monitor::ComputeAndPublish() noexcept {
    if (!fft_initialized_) {
        return false;
    }

    MonitorResult result{};
    if (!ComputeStats(result)) {
        return false;
    }

    if (!ComputeNaturalFrequency(result)) {
        return false;
    }

    if (callback_ != nullptr) {
        callback_(callback_ctx_, result);
    }

    return true;
}

bool Monitor::ComputeStats(MonitorResult& result) const noexcept {
    const std::size_t count = BufferSize();
    if (count == 0U) {
        return false;
    }

    float roll_mean = 0.0f;
    float roll_m2 = 0.0f;
    float pitch_mean = 0.0f;
    float pitch_m2 = 0.0f;

    for (std::size_t i = 0U; i < count; ++i) {
        const std::size_t idx = PhysicalIndex(i);
        const float roll = roll_history_[idx];
        const float pitch = pitch_history_[idx];

        const float roll_delta = roll - roll_mean;
        roll_mean += roll_delta / static_cast<float>(i + 1U);
        const float roll_delta2 = roll - roll_mean;
        roll_m2 += roll_delta * roll_delta2;

        const float pitch_delta = pitch - pitch_mean;
        pitch_mean += pitch_delta / static_cast<float>(i + 1U);
        const float pitch_delta2 = pitch - pitch_mean;
        pitch_m2 += pitch_delta * pitch_delta2;
    }

    result.roll_mean = roll_mean;
    result.pitch_mean = pitch_mean;
    result.roll_variance = (count > 1U) ? (roll_m2 / static_cast<float>(count - 1U)) : 0.0f;
    result.pitch_variance = (count > 1U) ? (pitch_m2 / static_cast<float>(count - 1U)) : 0.0f;
    result.sample_count = static_cast<std::uint32_t>(count);
    result.timestamp_us = static_cast<std::uint64_t>(esp_timer_get_time());

    return true;
}

bool Monitor::ComputeNaturalFrequency(MonitorResult& result) noexcept {
    const std::size_t count = BufferSize();
    if (count < kFftWindowSamples) {
        return false;
    }

    const std::size_t step = kFftWindowSamples - kFftOverlapSamples;
    const std::size_t segments = ((count - kFftWindowSamples) / step) + 1U;
    const std::size_t span = kFftWindowSamples + (segments - 1U) * step;
    const std::size_t base_start = count - span;

    psd_accum_.fill(0.0f);

    for (std::size_t seg = 0U; seg < segments; ++seg) {
        const std::size_t seg_start = base_start + (seg * step);

        for (std::size_t i = 0U; i < kFftWindowSamples; ++i) {
            const std::size_t idx = PhysicalIndex(seg_start + i);
            const float roll = roll_history_[idx];
            const float pitch = pitch_history_[idx];
            const float magnitude = std::sqrt((roll * roll) + (pitch * pitch));

            const float window = 0.5f - 0.5f *
                std::cos(kTwoPi * static_cast<float>(i) / static_cast<float>(kFftWindowSamples - 1U));
            fft_input_[2U * i] = magnitude * window;
            fft_input_[2U * i + 1U] = 0.0f;
        }

        if (dsps_fft2r_fc32(fft_input_.data(), static_cast<int>(kFftWindowSamples)) != ESP_OK) {
            return false;
        }
        if (dsps_bit_rev_fc32(fft_input_.data(), static_cast<int>(kFftWindowSamples)) != ESP_OK) {
            return false;
        }

        for (std::size_t bin = 1U; bin < (kFftWindowSamples / 2U); ++bin) {
            const float real = fft_input_[2U * bin];
            const float imag = fft_input_[2U * bin + 1U];
            psd_accum_[bin] += (real * real) + (imag * imag);
        }
    }

    float max_val = 0.0f;
    std::size_t max_bin = 0U;
    const float inv_segments = 1.0f / static_cast<float>(segments);

    for (std::size_t bin = 1U; bin < (kFftWindowSamples / 2U); ++bin) {
        const float value = psd_accum_[bin] * inv_segments;
        if (value > max_val) {
            max_val = value;
            max_bin = bin;
        }
    }

    const float sample_rate_hz = static_cast<float>(CONFIG_MONITOR_IMU_RATE_HZ);
    result.natural_freq_hz = (static_cast<float>(max_bin) * sample_rate_hz) /
                             static_cast<float>(kFftWindowSamples);

    return true;
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

} // namespace monitor
