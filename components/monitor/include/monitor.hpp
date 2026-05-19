#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "sdkconfig.h"
#include "lsm6ds3.hpp"
#include "complementary_filter.hpp"

namespace monitor {

constexpr std::size_t kStorageSamples =
    static_cast<std::size_t>(CONFIG_MONITOR_STORAGE_MINUTES) * 60U *
    static_cast<std::size_t>(CONFIG_MONITOR_IMU_RATE_HZ);

constexpr std::size_t kFftWindowSamples = 1024U;
constexpr std::size_t kFftOverlapSamples = kFftWindowSamples / 2U;

struct MonitorConfig {
    float filter_alpha{0.98f};
};

struct MonitorResult {
    float roll_mean{0.0f};
    float roll_variance{0.0f};
    float pitch_mean{0.0f};
    float pitch_variance{0.0f};
    float natural_freq_hz{0.0f};
    std::uint32_t sample_count{0};
    std::uint64_t timestamp_us{0};
};

using EventCb = void(*)(void* ctx, const MonitorResult& result);

class Monitor {
public:
    explicit Monitor(const sensor::Lsm6ds3::Config& imu_config,
                     const MonitorConfig& config = {}) noexcept;

    [[nodiscard]] bool Init() noexcept;
    void RegisterCallback(EventCb cb, void* ctx) noexcept;
    [[nodiscard]] bool Update(float dt_s) noexcept;

private:
    [[nodiscard]] bool ReadImu(sensor::lsm6ds3::Value& gyro,
                               sensor::lsm6ds3::Value& accel) noexcept;
    void PushSample(float roll, float pitch) noexcept;
    [[nodiscard]] bool ComputeAndPublish() noexcept;
    [[nodiscard]] bool ComputeStats(MonitorResult& result) const noexcept;
    [[nodiscard]] bool ComputeNaturalFrequency(MonitorResult& result) noexcept;
    [[nodiscard]] std::size_t BufferSize() const noexcept;
    [[nodiscard]] std::size_t StartIndex() const noexcept;
    [[nodiscard]] std::size_t PhysicalIndex(std::size_t logical_index) const noexcept;

    sensor::Lsm6ds3 imu_;
    filter::Complementary filter_;
    MonitorConfig config_;
    EventCb callback_{nullptr};
    void* callback_ctx_{nullptr};

    std::array<float, kStorageSamples> roll_history_{};
    std::array<float, kStorageSamples> pitch_history_{};
    std::size_t write_index_{0U};
    std::size_t sample_count_{0U};

    std::array<float, kFftWindowSamples * 2U> fft_input_{};
    std::array<float, kFftWindowSamples / 2U> psd_accum_{};
    bool fft_initialized_{false};
};

} // namespace monitor
