#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"
#include "nvs.h"
#include "lsm6ds3.hpp"
#include "adaptive_complementary_filter.hpp"
#include "calibration.hpp"
#include "monitor_events.hpp"

namespace monitor {

constexpr std::size_t kStorageSamples =
    static_cast<std::size_t>(CONFIG_MONITOR_STORAGE_MINUTES) * 60U *
    static_cast<std::size_t>(CONFIG_MONITOR_IMU_RATE_HZ);

constexpr std::size_t kFftWindowSamples = 1024U;
constexpr std::size_t kFftOverlapSamples = kFftWindowSamples / 2U;
constexpr std::size_t kEventSamples = 2048U;

static_assert(kStorageSamples >= kFftWindowSamples,
              "Monitor storage window must be >= FFT window.");
static_assert(kStorageSamples <= 32768U,
              "Monitor storage window exceeds bounded RAM budget.");
static_assert(CONFIG_MONITOR_SHORT_BUFFER_SIZE <= 1024,
              "Monitor short buffer exceeds bounded RAM budget.");
static_assert(CONFIG_MONITOR_N_DPAD < kStorageSamples,
              "DISTURBED refresh margin must be smaller than storage window.");
static_assert(CONFIG_MONITOR_N_DPAD < kEventSamples,
              "DISTURBED refresh margin must be smaller than event window.");

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
};

struct MonitorConfig {
    float filter_alpha_base{0.98f};
    float filter_k_gain{50.0f};
    std::int32_t ae_gpio_pin{-1};
    std::int32_t ae_adc_channel{-1};
    int ae_adc_threshold{CONFIG_MONITOR_AE_ADC_THRESHOLD};
    float peak_min_amplitude_deg{static_cast<float>(CONFIG_MONITOR_PEAK_MIN_AMPLITUDE_X10) / 10.0f};
    std::size_t peak_min_spacing{static_cast<std::size_t>(CONFIG_MONITOR_PEAK_MIN_SPACING_SAMPLES)};
    float centerline_min_amplitude_deg{static_cast<float>(CONFIG_MONITOR_CENTERLINE_MIN_AMPLITUDE_X100) / 100.0f};
    float centerline_lobe_reversal_deg{static_cast<float>(CONFIG_MONITOR_CENTERLINE_LOBE_REVERSAL_X100) / 100.0f};
    float modal_freq_min_hz{static_cast<float>(CONFIG_MONITOR_MODAL_FREQ_MIN_HZ_X10) / 10.0f};
    float modal_freq_max_hz{static_cast<float>(CONFIG_MONITOR_MODAL_FREQ_MAX_HZ_X10) / 10.0f};
    float dsp_tkeo_high{static_cast<float>(CONFIG_MONITOR_DSP_TKEO_HIGH_X10) / 10.0f};
    float dsp_tkeo_low{static_cast<float>(CONFIG_MONITOR_DSP_TKEO_LOW_X10) / 10.0f};
    float dsp_gmag_onset_dps{static_cast<float>(CONFIG_MONITOR_DSP_GMAG_ONSET_X100) / 100.0f};
    float dsp_gmag_quiet_dps{static_cast<float>(CONFIG_MONITOR_DSP_GMAG_QUIET_X100) / 100.0f};
    std::size_t dsp_quiet_debounce{static_cast<std::size_t>(CONFIG_MONITOR_DISTURBED_EXIT_DEBOUNCE)};
};

enum class NodeState : std::uint8_t {
    IDLE = 0,
    DISTURBED = 1
};

struct MonitorResult {
    static constexpr std::size_t kDampingConfidenceMax = 8U;
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
    std::array<char, kDampingConfidenceMax> damping_confidence{'l', 'o', 'w', '\0'};
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

class TkeoWindow {
public:
    [[nodiscard]] bool Push(float gmag, float& out_tkeo) noexcept;
    void Reset() noexcept;
    [[nodiscard]] std::size_t Count() const noexcept { return count_; }

private:
    std::array<float, 3U> samples_{};
    std::size_t count_{0U};
};

class DspDisturbanceDetector {
public:
    void Reset() noexcept;
    [[nodiscard]] NodeState Update(float gmag, float tkeo, const MonitorConfig& config) noexcept;
    [[nodiscard]] NodeState State() const noexcept { return state_; }
    [[nodiscard]] std::size_t QuietCount() const noexcept { return quiet_count_; }

private:
    NodeState state_{NodeState::IDLE};
    std::size_t quiet_count_{0U};
};

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
    [[nodiscard]] TaskHandle_t GetTaskHandle() const noexcept { return task_handle_; }
    [[nodiscard]] std::uint32_t DroppedResultEvents() const noexcept { return dropped_result_events_; }
    [[nodiscard]] std::uint32_t DroppedFailureEvents() const noexcept { return dropped_failure_events_; }
    [[nodiscard]] std::uint32_t PendingAeEvents() const noexcept { return pending_ae_events_; }
    void TaskLoop() noexcept;

    void SetCalibrationBiases(const calibration::CalibrationBias& biases) noexcept;

private:
    [[nodiscard]] bool ReadImu(sensor::lsm6ds3::Value& gyro,
                               sensor::lsm6ds3::Value& accel) noexcept;
    void PushSample(float roll, float pitch,
                    float gx, float gy, float gz,
                    float ax, float ay, float az,
                    float gmag, float tkeo) noexcept;
    [[nodiscard]] bool ComputeAndPublish(NodeState pub_state, bool is_exit = true) noexcept;
    [[nodiscard]] bool ComputeStats(MonitorResult& result) const noexcept;
    [[nodiscard]] bool ComputeNaturalFrequency(MonitorResult& result) noexcept;
    [[nodiscard]] float ComputeAxisNaturalFrequency(const std::array<float, kStorageSamples>& history, std::size_t start_phys_idx, std::size_t count) noexcept;
    [[nodiscard]] float ComputeGmagNaturalFrequency() noexcept;
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
    enum class ExtremaKind : std::uint8_t {
        Peak = 0U,
        Trough = 1U
    };
    struct ExtremaPoint {
        std::size_t logical_index{0U};
        float value{0.0f};
        ExtremaKind kind{ExtremaKind::Peak};
    };
    struct ExtremaList {
        static constexpr std::size_t kMaxExtrema = PeakList::kMaxPeaks;
        std::array<ExtremaPoint, kMaxExtrema> points{};
        std::size_t count{0U};
    };
    struct CenterlinePair {
        std::size_t center_logical_index{0U};
        float center_value{0.0f};
        float amplitude{0.0f};
        float time_s{0.0f};
    };
    struct CenterlinePairList {
        static constexpr std::size_t kMaxPairs = PeakList::kMaxPeaks;
        std::array<CenterlinePair, kMaxPairs> pairs{};
        std::size_t count{0U};
    };
    struct FftBinRange {
        std::size_t min_bin{0U};
        std::size_t max_bin{0U};
        bool valid{false};
    };
    enum class DecayQuality : std::uint8_t {
        None = 0U,
        Low = 1U,
        Reliable = 2U
    };
    enum class DominantAxis : std::uint8_t {
        X = 0U,
        Y = 1U,
        Z = 2U
    };
    struct DecayOnsetResult {
        std::size_t onset{0U};
        DecayQuality quality{DecayQuality::None};
    };
    struct SwayAxisResult {
        float sx_deg{0.0f};
        float sy_deg{0.0f};
        float sz_deg{0.0f};
        DominantAxis dominant{DominantAxis::X};
        bool valid{false};
    };
    struct DampingFitResult {
        float damping_ratio{0.0f};
        std::array<char, MonitorResult::kDampingConfidenceMax> confidence{'l', 'o', 'w', '\0'};
    };
    struct EventAnalysisResult {
        float natural_freq_hz{0.0f};
        float damping_ratio{0.0f};
        std::array<char, MonitorResult::kDampingConfidenceMax> damping_confidence{'l', 'o', 'w', '\0'};
    };
    struct ModalAxisResult {
        DecayRegion decay{};
        ExtremaList raw_extrema{};
        ExtremaList collapsed_extrema{};
        CenterlinePairList centerline_pairs{};
        PeakList pair_envelope{};
        std::size_t residual_count{0U};
        float natural_freq_hz{0.0f};
        float damping_ratio{0.0f};
    };
    [[nodiscard]] DecayRegion FindDecayRegion(const std::array<float, kStorageSamples>& data, PeakList& out_peaks) const noexcept;
    [[nodiscard]] float ComputeDampingRegression(const PeakList& peaks, float natural_freq_hz) const noexcept;
    [[nodiscard]] bool DetectRawExtrema(const std::array<float, kStorageSamples>& data, std::size_t start_logical,
                                        std::size_t count, ExtremaList& out_extrema) const noexcept;
    [[nodiscard]] bool CollapseExtremaLobes(const ExtremaList& raw_extrema, ExtremaList& out_extrema) const noexcept;
    [[nodiscard]] bool BuildCenterlinePairs(const ExtremaList& extrema, CenterlinePairList& out_pairs) const noexcept;
    [[nodiscard]] bool SubtractCenterline(const std::array<float, kStorageSamples>& data, std::size_t start_logical,
                                          std::size_t count, const CenterlinePairList& pairs) noexcept;
    [[nodiscard]] bool SelectPairEnvelope(const CenterlinePairList& pairs, PeakList& out_envelope) const noexcept;
    [[nodiscard]] FftBinRange SelectFftBinRange(std::size_t fft_size, float sample_rate_hz) const noexcept;
    [[nodiscard]] float ComputeResidualNaturalFrequency(const float* residual, std::size_t count) noexcept;
    void AnalyzeModalAxis(const std::array<float, kStorageSamples>& data, ModalAxisResult& out) noexcept;
    [[nodiscard]] DecayOnsetResult FindDecayOnsetTkeo() const noexcept;
    [[nodiscard]] SwayAxisResult ComputeDominantAxisSway(std::size_t start, std::size_t end) const noexcept;
    [[nodiscard]] float ComputeSignedAxisNaturalFrequency(DominantAxis axis, std::size_t start, std::size_t count) noexcept;
    [[nodiscard]] DampingFitResult ComputePeakHoldDamping(std::size_t start, std::size_t count,
                                                           float natural_freq_hz,
                                                           DecayQuality quality) noexcept;
    [[nodiscard]] EventAnalysisResult AnalyzeImuEvent() noexcept;
    static void SetConfidence(std::array<char, MonitorResult::kDampingConfidenceMax>& dst,
                              const char* src) noexcept;

#if CONFIG_MONITOR_DEBUG_DUMP
    void DumpDebugToSD(const ModalAxisResult& roll_modal, const ModalAxisResult& pitch_modal,
                       float freq_roll_hz, float freq_pitch_hz,
                       float zeta_roll, float zeta_pitch,
                       std::int64_t modal_elapsed_us) noexcept;
#endif

    void CheckFailureEvents() noexcept;
    void PublishFailure(FailureEvent event) noexcept;
    static void AeGpioIsr(void* arg) noexcept;
    [[nodiscard]] std::size_t BufferSize() const noexcept;
    [[nodiscard]] std::size_t StartIndex() const noexcept;
    [[nodiscard]] std::size_t PhysicalIndex(std::size_t logical_index) const noexcept;

    sensor::Lsm6ds3 imu_;
    filter::AdaptiveComplementary filter_;
    MonitorConfig config_;
    calibration::CalibrationBias calib_bias_{};
    nvs_handle_t calib_nvs_handle_{0};

    mutable std::mutex mutex_;
    TaskHandle_t task_handle_{nullptr};

    std::array<float, kStorageSamples> roll_history_{};
    std::array<float, kStorageSamples> pitch_history_{};
    std::array<float, kStorageSamples> gmag_history_{};
    std::array<float, kEventSamples> gx_history_{};
    std::array<float, kEventSamples> gy_history_{};
    std::array<float, kEventSamples> gz_history_{};
    std::array<float, kEventSamples> ax_history_{};
    std::array<float, kEventSamples> ay_history_{};
    std::array<float, kEventSamples> az_history_{};
    std::size_t write_index_{0U};
    std::size_t sample_count_{0U};

    static constexpr std::size_t kShortBufferSamples = static_cast<std::size_t>(CONFIG_MONITOR_SHORT_BUFFER_SIZE);
    std::array<float, kShortBufferSamples> roll_short_{};
    std::array<float, kShortBufferSamples> pitch_short_{};
    std::array<float, kShortBufferSamples> gmag_short_{};
    std::array<float, kShortBufferSamples> gx_short_{};
    std::array<float, kShortBufferSamples> gy_short_{};
    std::array<float, kShortBufferSamples> gz_short_{};
    std::array<float, kShortBufferSamples> ax_short_{};
    std::array<float, kShortBufferSamples> ay_short_{};
    std::array<float, kShortBufferSamples> az_short_{};
    std::size_t short_write_index_{0U};
    std::size_t short_sample_count_{0U};

    float roll_short_sum_{0.0f};
    float roll_short_sq_sum_{0.0f};
    float pitch_short_sum_{0.0f};
    float pitch_short_sq_sum_{0.0f};

    std::size_t disturbed_exit_debounce_counter_{0U};
    TkeoWindow tkeo_window_{};
    DspDisturbanceDetector dsp_detector_{};


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
    PeakList roll_peaks_{};
    PeakList pitch_peaks_{};
    std::array<float, kStorageSamples> residual_scratch_{};
    ModalAxisResult roll_modal_scratch_{};
    ModalAxisResult pitch_modal_scratch_{};
    bool fft_initialized_{false};

    void* adc_handle_{nullptr};
    bool adc_initialized_{false};
    portMUX_TYPE ae_mux_ = portMUX_INITIALIZER_UNLOCKED;
    std::uint32_t pending_ae_events_{0U};
    std::uint32_t dropped_result_events_{0U};
    std::uint32_t dropped_failure_events_{0U};

    static constexpr std::size_t kMaxStreamSamples = 20U;
    std::array<StreamSample, kMaxStreamSamples> stream_samples_{};
    std::size_t stream_write_index_{0U};
    std::size_t stream_count_{0U};
};

} // namespace monitor
