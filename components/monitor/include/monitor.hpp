#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include "sdkconfig.h"
#include "lsm6ds3.hpp"
#include "complementary_filter.hpp"
#include "monitor_events.hpp"

namespace monitor {

constexpr std::size_t kStorageSamples =
    static_cast<std::size_t>(CONFIG_MONITOR_STORAGE_MINUTES) * 60U *
    static_cast<std::size_t>(CONFIG_MONITOR_IMU_RATE_HZ);

constexpr std::size_t kFftWindowSamples = 1024U;
constexpr std::size_t kFftOverlapSamples = kFftWindowSamples / 2U;

enum class NodeState : std::uint8_t {
    IDLE = 0,
    DISTURBED = 1
};

struct StreamSample {
    float accel_x{0.0f};
    float accel_y{0.0f};
    float accel_z{0.0f};
    float gyro_x{0.0f};
    float gyro_y{0.0f};
    float gyro_z{0.0f};
    float roll{0.0f};
    float pitch{0.0f};
    std::uint64_t timestamp_us{0};
    NodeState state{NodeState::IDLE};
};

struct MonitorConfig {
    float filter_alpha{0.98f};
    std::int32_t ae_gpio_pin{-1};
    std::int32_t ae_adc_channel{-1};
    int ae_adc_threshold{CONFIG_MONITOR_AE_ADC_THRESHOLD};
    float peak_min_amplitude_deg{static_cast<float>(CONFIG_MONITOR_PEAK_MIN_AMPLITUDE_X10) / 10.0f};
    std::size_t peak_min_spacing{static_cast<std::size_t>(CONFIG_MONITOR_PEAK_MIN_SPACING_SAMPLES)};
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
    float natural_freq_roll_hz{0.0f};
    float natural_freq_pitch_hz{0.0f};
    NodeState state{NodeState::IDLE};
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
    [[nodiscard]] bool Start() noexcept;
    [[nodiscard]] bool Update(float dt_s) noexcept;
    [[nodiscard]] bool ReadImuSample(sensor::lsm6ds3::Value& gyro,
                                     sensor::lsm6ds3::Value& accel) noexcept;

    void GetFftData(float* out_psd, std::size_t& out_len) const noexcept;
    void GetTiltHistory(float* out_roll, float* out_pitch, std::size_t& out_len, std::size_t max_len) const noexcept;
    void GetLatestSamples(StreamSample* out_samples, std::size_t& out_len, std::size_t max_len) const noexcept;
    [[nodiscard]] NodeState GetState() const noexcept { return state_; }
    void TaskLoop() noexcept;

private:
    [[nodiscard]] bool ReadImu(sensor::lsm6ds3::Value& gyro,
                               sensor::lsm6ds3::Value& accel) noexcept;
    void PushSample(float roll, float pitch, float ax, float ay, float az) noexcept;
    [[nodiscard]] bool ComputeAndPublish(NodeState pub_state, bool is_exit = true) noexcept;
    [[nodiscard]] bool ComputeStats(MonitorResult& result) const noexcept;
    [[nodiscard]] bool ComputeNaturalFrequency(MonitorResult& result) noexcept;
    [[nodiscard]] float ComputeAxisNaturalFrequency(const std::array<float, kStorageSamples>& history, std::size_t start_phys_idx, std::size_t count) noexcept;
    [[nodiscard]] bool ComputeSwayAndDamping(MonitorResult& result) noexcept;
    
    struct DecayRegion {
        std::size_t start_index{0U};
        std::size_t count{0U};
    };
    struct PeakList {
        static constexpr std::size_t kMaxPeaks = 256U;
        std::array<float, kMaxPeaks> amplitudes{};
        std::array<float, kMaxPeaks> times{};
        std::size_t count{0U};
    };
    [[nodiscard]] DecayRegion FindDecayRegion(const std::array<float, kStorageSamples>& data, PeakList& out_peaks) const noexcept;
    [[nodiscard]] float ComputeDampingRegression(const PeakList& peaks, float natural_freq_hz) const noexcept;

    void CheckFailureEvents() noexcept;
    void PublishFailure(FailureEvent event) noexcept;
    static void AeGpioIsr(void* arg) noexcept;
    [[nodiscard]] std::size_t BufferSize() const noexcept;
    [[nodiscard]] std::size_t StartIndex() const noexcept;
    [[nodiscard]] std::size_t PhysicalIndex(std::size_t logical_index) const noexcept;

    sensor::Lsm6ds3 imu_;
    filter::Complementary filter_;
    MonitorConfig config_;

    mutable std::mutex mutex_;
    void* task_handle_{nullptr};

    std::array<float, kStorageSamples> roll_history_{};
    std::array<float, kStorageSamples> pitch_history_{};
    std::size_t write_index_{0U};
    std::size_t sample_count_{0U};

    static constexpr std::size_t kShortBufferSamples = static_cast<std::size_t>(CONFIG_MONITOR_SHORT_BUFFER_SIZE);
    std::array<float, kShortBufferSamples> roll_short_{};
    std::array<float, kShortBufferSamples> pitch_short_{};
    std::size_t short_write_index_{0U};
    std::size_t short_sample_count_{0U};

    float roll_short_sum_{0.0f};
    float roll_short_sq_sum_{0.0f};
    float pitch_short_sum_{0.0f};
    float pitch_short_sq_sum_{0.0f};

    static constexpr std::size_t kAccelErrShortBufferSamples = static_cast<std::size_t>(CONFIG_MONITOR_ACCEL_ERR_SHORT_BUF_SIZE);
    std::array<float, kAccelErrShortBufferSamples> accel_err_short_{};
    std::size_t accel_err_short_write_index_{0U};
    std::size_t accel_err_short_sample_count_{0U};
    float accel_err_short_sum_{0.0f};
    float accel_err_short_sq_sum_{0.0f};
    float accel_err_baseline_var_{0.0f};
    bool has_accel_err_baseline_{false};
    float baseline_accum_sum_{0.0f};
    float baseline_accum_sq_sum_{0.0f};
    std::size_t baseline_sample_count_{0U};

    std::size_t disturbed_exit_debounce_counter_{0U};


    NodeState state_{NodeState::IDLE};
    float idle_5min_roll_var_{0.0f};
    float idle_5min_pitch_var_{0.0f};
    bool has_baseline_variance_{false};

    bool taring_complete_{false};
    float roll_offset_{0.0f};
    float pitch_offset_{0.0f};
    float roll_tare_sum_{0.0f};
    float pitch_tare_sum_{0.0f};
    std::size_t tare_samples_accumulated_{0U};
    std::size_t tare_settle_accumulated_{0U};

    std::array<float, kFftWindowSamples * 2U> fft_input_{};
    std::array<float, kFftWindowSamples / 2U> psd_accum_{};
    bool fft_initialized_{false};

    void* adc_handle_{nullptr};
    bool adc_initialized_{false};
    volatile bool ae_gpio_event_{false};

    static constexpr std::size_t kMaxStreamSamples = 20U;
    std::array<StreamSample, kMaxStreamSamples> stream_samples_{};
    std::size_t stream_write_index_{0U};
    std::size_t stream_count_{0U};
};

} // namespace monitor
