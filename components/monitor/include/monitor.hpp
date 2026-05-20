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

#ifdef CONFIG_MONITOR_DEBUG
constexpr bool kMonitorDebugDefault = true;
#else
constexpr bool kMonitorDebugDefault = false;
#endif

struct MonitorConfig {
    float filter_alpha{0.98f};
    std::int32_t ae_gpio_pin{-1};
    std::int32_t ae_adc_channel{-1};
    int ae_adc_threshold{CONFIG_MONITOR_AE_ADC_THRESHOLD};
    float peak_min_amplitude_deg{static_cast<float>(CONFIG_MONITOR_PEAK_MIN_AMPLITUDE)};
    std::size_t peak_min_spacing{static_cast<std::size_t>(CONFIG_MONITOR_PEAK_MIN_SPACING_SAMPLES)};
    bool debug_enabled{kMonitorDebugDefault};
};

struct MonitorResult {
    float roll_mean{0.0f};
    float roll_variance{0.0f};
    float pitch_mean{0.0f};
    float pitch_variance{0.0f};
    float roll_sway_pp_max{0.0f};
    float roll_sway_pp_mean{0.0f};
    float pitch_sway_pp_max{0.0f};
    float pitch_sway_pp_mean{0.0f};
    float roll_damping_ratio{0.0f};
    float pitch_damping_ratio{0.0f};
    float natural_freq_hz{0.0f};
    std::uint32_t sample_count{0};
    std::uint64_t timestamp_us{0};
};

using EventCb = void(*)(void* ctx, const MonitorResult& result);

enum class FailureEvent : std::uint8_t {
    FreeFall = 0,
    AcousticEmission = 1
};

struct FailureResult {
    FailureEvent event{FailureEvent::FreeFall};
    std::uint64_t timestamp_us{0};
};

using FailureCb = void(*)(void* ctx, const FailureResult& result);

class Monitor {
public:
    explicit Monitor(const sensor::Lsm6ds3::Config& imu_config,
                     const MonitorConfig& config = {}) noexcept;

    [[nodiscard]] bool Init() noexcept;
    void RegisterCallback(EventCb cb, void* ctx) noexcept;
    void RegisterFailureCallback(FailureCb cb, void* ctx) noexcept;
    [[nodiscard]] bool Update(float dt_s) noexcept;

private:
    [[nodiscard]] bool ReadImu(sensor::lsm6ds3::Value& gyro,
                               sensor::lsm6ds3::Value& accel) noexcept;
    void PushSample(float roll, float pitch) noexcept;
    [[nodiscard]] bool ComputeAndPublish() noexcept;
    [[nodiscard]] bool ComputeStats(MonitorResult& result) const noexcept;
    [[nodiscard]] bool ComputeNaturalFrequency(MonitorResult& result) noexcept;
    [[nodiscard]] bool ComputeSwayAndDamping(MonitorResult& result) noexcept;
    void CheckFailureEvents() noexcept;
    void PublishFailure(FailureEvent event) noexcept;
    static void AeGpioIsr(void* arg) noexcept;
    [[nodiscard]] std::size_t BufferSize() const noexcept;
    [[nodiscard]] std::size_t StartIndex() const noexcept;
    [[nodiscard]] std::size_t PhysicalIndex(std::size_t logical_index) const noexcept;

    sensor::Lsm6ds3 imu_;
    filter::Complementary filter_;
    MonitorConfig config_;
    EventCb callback_{nullptr};
    void* callback_ctx_{nullptr};
    FailureCb failure_callback_{nullptr};
    void* failure_callback_ctx_{nullptr};

    std::array<float, kStorageSamples> roll_history_{};
    std::array<float, kStorageSamples> pitch_history_{};
    std::size_t write_index_{0U};
    std::size_t sample_count_{0U};

    std::array<float, kFftWindowSamples * 2U> fft_input_{};
    std::array<float, kFftWindowSamples / 2U> psd_accum_{};
    bool fft_initialized_{false};

    void* adc_handle_{nullptr};
    bool adc_initialized_{false};
    volatile bool ae_gpio_event_{false};
};

} // namespace monitor
