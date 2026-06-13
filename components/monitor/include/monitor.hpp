/// @file monitor.hpp
/// @brief Tree branch disturbance detection and modal analysis engine.
/// @details Processes calibrated IMU data through a real-time detection pipeline
/// (TKEO energy operator + Schmitt trigger state machine) and a post-hoc modal
/// analysis pipeline (TKEO decay onset, signed-axis FFT, peak-hold envelope,
/// OLS log-fit damping). Publishes MonitorResult on state transitions and
/// FailureResult on hardware failures.
/// @ingroup monitor

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

/// @brief Total samples stored in the rolling history buffer.
/// Computed as STORAGE_MINUTES * 60 * IMU_RATE_HZ.
constexpr std::size_t kStorageSamples =
    static_cast<std::size_t>(CONFIG_MONITOR_STORAGE_MINUTES) * 60U *
    static_cast<std::size_t>(CONFIG_MONITOR_IMU_RATE_HZ);

/// @brief FFT window size for natural frequency estimation.
constexpr std::size_t kFftWindowSamples = 1024U;
/// @brief FFT overlap for Welch averaging (half window).
constexpr std::size_t kFftOverlapSamples = kFftWindowSamples / 2U;
/// @brief Maximum disturbance event sample storage.
constexpr std::size_t kEventSamples = 2048U;
/// @brief Maximum AE spectral FFT window for persistent buffers.
constexpr std::size_t kAeSpectralMaxWindowSamples =
    static_cast<std::size_t>(CONFIG_MONITOR_AE_SPECTRAL_WINDOW_SIZE);
/// @brief Maximum AE spectral gradient history for persistent buffers.
constexpr std::size_t kAeSpectralMaxGradientSamples =
    static_cast<std::size_t>(CONFIG_MONITOR_AE_SPECTRAL_GRADIENT_WINDOW);

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
static_assert(kAeSpectralMaxWindowSamples >= 64U,
              "AE spectral window too small.");
static_assert((kAeSpectralMaxWindowSamples & (kAeSpectralMaxWindowSamples - 1U)) == 0U,
              "AE spectral window must be a power of two.");
static_assert(kAeSpectralMaxWindowSamples <= 1024U,
              "AE spectral window exceeds bounded RAM budget.");
static_assert(kAeSpectralMaxGradientSamples <= 256U,
              "AE spectral gradient window exceeds bounded RAM budget.");

/// @brief Single IMU sample snapshot for dashboard streaming.
struct StreamSample {
    float accel_x{0.0f};        ///< Calibrated X-axis acceleration [g].
    float accel_y{0.0f};        ///< Calibrated Y-axis acceleration [g].
    float accel_z{0.0f};        ///< Calibrated Z-axis acceleration [g].
    float gyro_x{0.0f};         ///< Calibrated X-axis angular velocity [dps].
    float gyro_y{0.0f};         ///< Calibrated Y-axis angular velocity [dps].
    float gyro_z{0.0f};         ///< Calibrated Z-axis angular velocity [dps].
    float roll{0.0f};           ///< Roll angle [deg].
    float pitch{0.0f};          ///< Pitch angle [deg].
    std::uint64_t timestamp_us{0}; ///< Monotonic system time [us].
};

/// @brief Runtime configuration for the Monitor.
/// All fields map to sdkconfig values and can be overridden at construction.
struct MonitorConfig {
    float filter_alpha_base{0.98f};          ///< Base gyro/accel blending ratio for adaptive complementary filter.
    float filter_k_gain{50.0f};              ///< Gain for adaptive weight calculation in complementary filter.
    std::int32_t ae_gpio_pin{-1};            ///< GPIO pin for acoustic emission sensor (GPIO mode).
    std::int32_t ae_adc_channel{-1};         ///< ADC channel for acoustic emission sensor (ADC mode).
    int ae_adc_threshold{CONFIG_MONITOR_AE_ADC_THRESHOLD}; ///< Raw ADC threshold for AE detection.
    float peak_min_amplitude_deg{static_cast<float>(CONFIG_MONITOR_PEAK_MIN_AMPLITUDE_X10) / 10.0f}; ///< Minimum peak amplitude for sway detection [deg].
    std::size_t peak_min_spacing{static_cast<std::size_t>(CONFIG_MONITOR_PEAK_MIN_SPACING_SAMPLES)}; ///< Minimum sample spacing between peaks.
    float modal_freq_min_hz{static_cast<float>(CONFIG_MONITOR_MODAL_FREQ_MIN_HZ_X10) / 10.0f}; ///< Minimum natural frequency for FFT band [Hz].
    float modal_freq_max_hz{static_cast<float>(CONFIG_MONITOR_MODAL_FREQ_MAX_HZ_X10) / 10.0f}; ///< Maximum natural frequency for FFT band [Hz].
    float dsp_tkeo_high{static_cast<float>(CONFIG_MONITOR_DSP_TKEO_HIGH_X10) / 10.0f};         ///< TKEO high threshold for DISTURBED entry.
    float dsp_tkeo_low{static_cast<float>(CONFIG_MONITOR_DSP_TKEO_LOW_X10) / 10.0f};           ///< TKEO low threshold for DISTURBED exit debounce.
    float dsp_gmag_onset_dps{static_cast<float>(CONFIG_MONITOR_DSP_GMAG_ONSET_X100) / 100.0f}; ///< Gyro magnitude threshold for DISTURBED entry [dps].
    float dsp_gmag_quiet_dps{static_cast<float>(CONFIG_MONITOR_DSP_GMAG_QUIET_X100) / 100.0f}; ///< Gyro magnitude threshold for DISTURBED exit debounce [dps].
    std::size_t dsp_quiet_debounce{static_cast<std::size_t>(CONFIG_MONITOR_DISTURBED_EXIT_DEBOUNCE)}; ///< Consecutive quiet samples required for IDLE transition.
    float noise_gate_gmag_dps{static_cast<float>(CONFIG_MONITOR_NOISE_GATE_GMAG_X10) / 10.0f}; ///< Minimum peak gyro magnitude for damping computation [dps].
    std::uint32_t spectral_sample_rate_hz{static_cast<std::uint32_t>(CONFIG_MONITOR_AE_SPECTRAL_SAMPLE_RATE_HZ)}; ///< AE spectral ADC sample rate [Hz].
    std::size_t spectral_window_size{static_cast<std::size_t>(CONFIG_MONITOR_AE_SPECTRAL_WINDOW_SIZE)}; ///< AE spectral FFT window length [samples].
    std::size_t spectral_bin_start{static_cast<std::size_t>(CONFIG_MONITOR_AE_SPECTRAL_BIN_START)}; ///< Inclusive AE spectral energy start bin.
    std::size_t spectral_bin_end{static_cast<std::size_t>(CONFIG_MONITOR_AE_SPECTRAL_BIN_END)}; ///< Inclusive AE spectral energy end bin.
    float spectral_leak_alpha{static_cast<float>(CONFIG_MONITOR_AE_SPECTRAL_LEAK_ALPHA_X100) / 100.0f}; ///< AE spectral leaking integrator alpha.
    float spectral_ewma_alpha{static_cast<float>(CONFIG_MONITOR_AE_SPECTRAL_EWMA_ALPHA_X100) / 100.0f}; ///< AE spectral EWMA baseline alpha.
    float spectral_danger_multiplier{static_cast<float>(CONFIG_MONITOR_AE_SPECTRAL_DANGER_MULTIPLIER_X10) / 10.0f}; ///< AE spectral dynamic threshold multiplier.
    std::size_t spectral_gradient_window{static_cast<std::size_t>(CONFIG_MONITOR_AE_SPECTRAL_GRADIENT_WINDOW)}; ///< AE spectral gradient history length.
    float spectral_jump_threshold{static_cast<float>(CONFIG_MONITOR_AE_SPECTRAL_JUMP_THRESHOLD_X10) / 10.0f}; ///< AE spectral energy-jump latch threshold.
    std::uint32_t spectral_latch_duration_ms{static_cast<std::uint32_t>(CONFIG_MONITOR_AE_SPECTRAL_LATCH_DURATION_MS)}; ///< AE spectral latch hold time [ms].
    std::uint32_t spectral_min_publish_interval_ms{static_cast<std::uint32_t>(CONFIG_MONITOR_AE_SPECTRAL_MIN_PUBLISH_INTERVAL_MS)}; ///< AE spectral repeat publish interval [ms].
};

/// @brief Disturbance state machine states.
enum class NodeState : std::uint8_t {
    IDLE = 0,       ///< No disturbance detected. Publishing 5-min tilt statistics.
    DISTURBED = 1   ///< Dynamic motion in progress. Buffering disturbance data.
};

/// @brief Published monitoring result payload.
/// Contains tilt statistics, sway amplitudes, natural frequency, damping ratio,
/// and state information. Published via MQTT and logged to SD on state transitions.
struct MonitorResult {
    static constexpr std::size_t kDampingConfidenceMax = 8U; ///< Max length of damping confidence string.
    float roll_mean{0.0f};          ///< Mean roll angle over analysis window [deg].
    float roll_variance{0.0f};      ///< Variance of roll angle over analysis window [deg^2].
    float pitch_mean{0.0f};         ///< Mean pitch angle over analysis window [deg].
    float pitch_variance{0.0f};     ///< Variance of pitch angle over analysis window [deg^2].
    float roll_sway_pp_max{0.0f};   ///< Maximum peak-to-peak roll sway during DISTURBED [deg].
    float roll_sway_pp_mean{0.0f};  ///< Mean peak-to-peak roll sway during DISTURBED [deg].
    float pitch_sway_pp_max{0.0f};  ///< Maximum peak-to-peak pitch sway during DISTURBED [deg].
    float pitch_sway_pp_mean{0.0f}; ///< Mean peak-to-peak pitch sway during DISTURBED [deg].
    float roll_damping_ratio{0.0f};  ///< Dominant-axis damping ratio (mirrored into roll field for legacy compat) [dimensionless].
    float pitch_damping_ratio{0.0f}; ///< Dominant-axis damping ratio (mirrored into pitch field for legacy compat) [dimensionless].
    float natural_freq_hz{0.0f};    ///< Dominant signed-gyro-axis natural frequency [Hz]. Zero for IDLE.
    float natural_freq_roll_hz{0.0f};  ///< Dominant-axis natural frequency (legacy roll field) [Hz].
    float natural_freq_pitch_hz{0.0f}; ///< Dominant-axis natural frequency (legacy pitch field) [Hz].
    std::array<char, kDampingConfidenceMax> damping_confidence{'l', 'o', 'w', '\0'}; ///< Envelope fit confidence: "high", "medium", "low".
    NodeState state{NodeState::IDLE}; ///< FSM state that produced this result.
    std::uint32_t sample_count{0};  ///< Number of samples in the analysis window.
    std::uint64_t timestamp_us{0};  ///< Monotonic system time when computed [us].
};

using EventCb = void(*)(void* ctx, const MonitorResult& result);

/// @brief Failure event types detected by hardware.
enum class FailureEvent : std::uint8_t {
    FreeFall = 0,          ///< LSM6DS3 free-fall motion interrupt triggered.
    AcousticEmission = 1   ///< Acoustic emission sensor GPIO/ADC threshold exceeded.
};

/// @brief Published failure event payload.
struct FailureResult {
    FailureEvent event{FailureEvent::FreeFall}; ///< Type of failure detected.
    std::uint64_t timestamp_us{0};              ///< Monotonic system time when detected [us].
};

using FailureCb = void(*)(void* ctx, const FailureResult& result);

/// @brief 3-sample sliding window Teager-Kaiser Energy Operator.
///
/// Computes TKEO with 1-sample latency:
///   psi[n] = x[n-1]^2 - x[n-2] * x[n]
///
/// @see imu_algorithms/_detection.py::tkeo_streaming
class TkeoWindow {
public:
    /// @brief Push a new gyro magnitude sample and optionally output TKEO value.
    /// @param gmag New gyro magnitude sample to push.
    /// @param out_tkeo Output TKEO value. Valid only when return is true.
    /// @return true when enough samples accumulated for valid TKEO output (3+ samples).
    [[nodiscard]] bool Push(float gmag, float& out_tkeo) noexcept;
    /// @brief Reset internal sample buffer and counter to zero.
    void Reset() noexcept;
    /// @brief Number of samples pushed so far (0-3).
    [[nodiscard]] std::size_t Count() const noexcept { return count_; }

private:
    std::array<float, 3U> samples_{};
    std::size_t count_{0U};
};

/// @brief Schmitt-trigger state machine for per-sample disturbance detection.
///
/// Two-state design with hysteresis (high/low TKEO thresholds) and a quiet
/// debounce counter in DISTURBED state. Prevents chatter during low-amplitude
/// oscillation phases.
///
/// Transitions:
/// - IDLE → DISTURBED: TKEO > high_thresh OR gmag > onset
/// - DISTURBED → IDLE: quiet_count >= debounce samples
/// - Quiet re-entry: TKEO > high_thresh OR gmag >= quiet_thresh resets counter
///
/// @see imu_algorithms/_detection.py::EventDetector
class DspDisturbanceDetector {
public:
    /// @brief Reset state machine to IDLE.
    void Reset() noexcept;
    /// @brief Process one sample through the state machine.
    /// @param gmag Gyro magnitude [dps].
    /// @param tkeo TKEO energy value from TkeoWindow.
    /// @param config Runtime configuration with threshold values.
    /// @return Current state after processing (IDLE or DISTURBED).
    [[nodiscard]] NodeState Update(float gmag, float tkeo, const MonitorConfig& config) noexcept;
    /// @brief Current state of the detector.
    [[nodiscard]] NodeState State() const noexcept { return state_; }
    /// @brief Number of consecutive quiet samples in DISTURBED state.
    [[nodiscard]] std::size_t QuietCount() const noexcept { return quiet_count_; }

private:
    NodeState state_{NodeState::IDLE};
    std::size_t quiet_count_{0U};
};

/// @brief Output of one AE spectral detector state update.
struct AeSpectralUpdateResult {
    float energy{0.0f};              ///< High-frequency energy from current FFT window.
    float gradient{0.0f};            ///< Leaking-integrator gradient over configured window.
    float danger_threshold{0.0f};    ///< Current adaptive danger threshold.
    float ewma_mean{0.0f};           ///< EWMA mean of normal gradients.
    float sigma{0.1f};               ///< EWMA standard deviation, clamped to >= 0.1.
    bool latch_active{false};        ///< Energy-jump latch currently active.
    bool latch_started{false};       ///< Latch transitioned inactive to active this update.
    bool danger_active{false};       ///< Adaptive gradient danger currently active.
    bool danger_started{false};      ///< Danger transitioned inactive to active this update.
    bool should_publish{false};      ///< Caller should publish FailureEvent::AcousticEmission.
};

/// @brief Bounded acoustic-emission spectral detector state machine.
class AeSpectralDetector {
public:
    /// @brief Validate runtime detector configuration against fixed buffers and FFT bounds.
    [[nodiscard]] static bool ValidateConfig(const MonitorConfig& config) noexcept;
    /// @brief Sum configured complex FFT magnitudes and divide by 1000.0.
    [[nodiscard]] static float ComputeEnergyFromComplexBins(const float* fft_data,
                                                            std::size_t fft_size,
                                                            std::size_t bin_start,
                                                            std::size_t bin_end) noexcept;
    /// @brief Apply Hamming window, run FFT, and compute high-frequency energy.
    [[nodiscard]] static bool ComputeWindowEnergy(const std::uint16_t* samples,
                                                  const MonitorConfig& config,
                                                  float* fft_buffer,
                                                  std::size_t fft_buffer_len,
                                                  float& out_energy) noexcept;
    /// @brief Reset detector history and active states.
    void Reset() noexcept;
    /// @brief Update latch, integrator, gradient, baseline, and publish gate from one energy sample.
    [[nodiscard]] AeSpectralUpdateResult UpdateEnergy(float energy,
                                                      std::uint64_t now_ms,
                                                      const MonitorConfig& config) noexcept;

private:
    std::array<float, kAeSpectralMaxGradientSamples> gradient_ring_{};
    std::size_t gradient_write_index_{0U};
    std::size_t gradient_count_{0U};
    float previous_energy_{0.0f};
    bool has_previous_energy_{false};
    float integrator_{0.0f};
    float ewma_mean_{0.0f};
    float ewma_variance_{0.01f};
    float sigma_{0.1f};
    std::uint64_t latch_until_ms_{0U};
    std::uint64_t last_publish_ms_{0U};
    bool latch_active_{false};
    bool danger_active_{false};
};

/// @brief Tree branch structural health monitor.
///
/// Coordinatesthe IMU sensor, adaptive complementary filter, disturbance
/// detection, and post-hoc modal analysis. Runs on a dedicated FreeRTOS task
/// (core 1, priority 5).
///
/// Data flow:
/// 1. Read calibrated IMU → adaptive complementary filter → roll/pitch
/// 2. Gyro magnitude → TKEO window → Schmitt trigger → IDLE/DISTURBED
/// 3. On DISTURBED→IDLE: TKEO decay onset → dominant axis FFT → peak-hold
///    envelope → OLS log-fit damping → publish MonitorResult
///
/// @see imu_algorithms/_extraction.py::Pipeline
class Monitor {
public:
    /// @brief Construct a Monitor with IMU configuration and optional runtime config.
    /// @param imu_config LSM6DS3 sensor configuration (read/write callbacks, ODR).
    /// @param config Runtime parameters; defaults to sdkconfig values.
    explicit Monitor(const sensor::Lsm6ds3::Config& imu_config,
                     const MonitorConfig& config = {}) noexcept;

    /// @brief Initialize the IMU, motion detection, AE sensor (GPIO or ADC), and NVS.
    /// @return true if all subsystems initialized successfully.
    [[nodiscard]] bool Init() noexcept;
    /// @brief Create the FreeRTOS monitor task and start sampling.
    /// @return true if task created successfully.
    [[nodiscard]] bool Start() noexcept;
    /// @brief Process one IMU sample through the detection pipeline.
    /// Called from the FreeRTOS task loop at the configured IMU rate.
    /// @param dt_s Time delta since last sample [s].
    /// @return true if sample processed successfully.
    [[nodiscard]] bool Update(float dt_s) noexcept;
    /// @brief Read one IMU sample from the sensor (for verification/debug).
    /// @param gyro Output gyroscope reading [dps].
    /// @param accel Output accelerometer reading [g].
    /// @return true if read successful.
    [[nodiscard]] bool ReadImuSample(sensor::lsm6ds3::Value& gyro,
                                     sensor::lsm6ds3::Value& accel) noexcept;

    /// @brief Copy accumulated PSD data for dashboard visualization.
    /// @param out_psd Output buffer for PSD values.
    /// @param out_len Number of PSD bins written.
    void GetFftData(float* out_psd, std::size_t& out_len) const noexcept;
    /// @brief Copy recent tilt history for dashboard charting.
    /// @param out_roll Output buffer for roll values [deg].
    /// @param out_pitch Output buffer for pitch values [deg].
    /// @param out_len Number of samples written.
    /// @param max_len Maximum samples to copy.
    void GetTiltHistory(float* out_roll, float* out_pitch, std::size_t& out_len, std::size_t max_len) const noexcept;
    /// @brief Copy latest stream samples for dashboard live display.
    /// @param out_samples Output buffer for stream samples.
    /// @param out_len Number of samples written.
    /// @param max_len Maximum samples to copy.
    void GetLatestSamples(StreamSample* out_samples, std::size_t& out_len, std::size_t max_len) const noexcept;
    /// @brief Current disturbance state (IDLE or DISTURBED).
    [[nodiscard]] NodeState GetState() const noexcept { return state_; }
    /// @brief FreeRTOS task handle of the monitor task.
    [[nodiscard]] TaskHandle_t GetTaskHandle() const noexcept { return task_handle_; }
    /// @brief FreeRTOS task handle of the AE spectral task when spectral mode is enabled.
    [[nodiscard]] TaskHandle_t GetAeSpectralTaskHandle() const noexcept { return ae_spectral_task_handle_; }
    /// @brief Count of dropped MonitorResult event posts.
    [[nodiscard]] std::uint32_t DroppedResultEvents() const noexcept { return dropped_result_events_; }
    /// @brief Count of dropped FailureResult event posts.
    [[nodiscard]] std::uint32_t DroppedFailureEvents() const noexcept { return dropped_failure_events_; }
    /// @brief Count of pending acoustic emission events.
    [[nodiscard]] std::uint32_t PendingAeEvents() const noexcept { return pending_ae_events_; }
    /// @brief FreeRTOS task entry point (called once, loops internally).
    void TaskLoop() noexcept;
    /// @brief FreeRTOS AE spectral task entry point (called once, loops internally).
    void AeSpectralTaskLoop() noexcept;

    /// @brief Set calibration biases and persist to NVS.
    /// @param biases Per-axis accel/gyro bias values.
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
    [[nodiscard]] bool ComputeSwayAndDamping(MonitorResult& result) noexcept;
    /// @brief Frequency bin range for FFT peak selection.
    struct FftBinRange {
        std::size_t min_bin{0U};  ///< Lowest FFT bin to scan.
        std::size_t max_bin{0U};  ///< Highest FFT bin to scan.
        bool valid{false};        ///< false if range computation failed.
    };
    /// @brief Decay region quality classification from TKEO burst validation.
    enum class DecayQuality : std::uint8_t {
        None = 0U,       ///< No usable decay region found.
        Low = 1U,         ///< Region found but insufficient samples or amplitude drop.
        Reliable = 2U     ///< Region meets all validation criteria.
    };
    /// @brief Dominant oscillation axis (largest integrated gyro).
    enum class DominantAxis : std::uint8_t {
        X = 0U,  ///< X-axis gyro dominant.
        Y = 1U,  ///< Y-axis gyro dominant.
        Z = 2U   ///< Z-axis gyro dominant.
    };
    /// @brief Result of TKEO energy-burst decay onset detection.
    struct DecayOnsetResult {
        std::size_t onset{0U};          ///< Sample index where free-decay begins.
        DecayQuality quality{DecayQuality::None}; ///< Validation quality of the detected region.
    };
    /// @brief Peak-to-peak angular displacement per axis and dominant axis selection.
    struct SwayAxisResult {
        float sx_deg{0.0f};             ///< Peak-to-peak X-axis angular displacement [deg].
        float sy_deg{0.0f};             ///< Peak-to-peak Y-axis angular displacement [deg].
        float sz_deg{0.0f};             ///< Peak-to-peak Z-axis angular displacement [deg].
        DominantAxis dominant{DominantAxis::X}; ///< Axis with largest displacement.
        bool valid{false};              ///< false if no valid sway detected.
    };
    /// @brief Result of peak-hold envelope OLS damping regression.
    struct DampingFitResult {
        float damping_ratio{0.0f};      ///< Estimated damping ratio zeta [0, 1].
        std::array<char, MonitorResult::kDampingConfidenceMax> confidence{'l', 'o', 'w', '\0'}; ///< Fit confidence.
    };
    /// @brief Combined result of post-hoc event analysis (AnalyzeImuEvent).
    struct EventAnalysisResult {
        float natural_freq_hz{0.0f};    ///< Dominant-axis natural frequency [Hz].
        float damping_ratio{0.0f};      ///< Damping ratio from peak-hold envelope OLS [0, 1].
        std::array<char, MonitorResult::kDampingConfidenceMax> damping_confidence{'l', 'o', 'w', '\0'}; ///< Fit confidence.
    };



    /// @brief Find free-decay onset using TKEO energy-burst detection.
    /// Algorithm from imu_algorithms/_envelope.py::find_decay_onset_tkeo.
    /// Computes non-negative TKEO over gmag history, finds last energy burst,
    /// snaps to nearest local maximum, and validates decay quality.
    /// @return DecayOnsetResult with onset index and quality classification.
    /// @see imu_algorithms/_envelope.py::find_decay_onset_tkeo
    [[nodiscard]] DecayOnsetResult FindDecayOnsetTkeo() const noexcept;
    /// @brief Integrate gyro axes over full buffer and select dominant axis
    /// via peak-to-peak angular displacement (max - min of cumulative angle).
    /// @param start Start sample index.
    /// @param end End sample index (exclusive).
    /// @return SwayAxisResult with per-axis peak-to-peak displacements and dominant axis.
    [[nodiscard]] SwayAxisResult ComputeDominantAxisSway(std::size_t start, std::size_t end) const noexcept;
    /// @brief Compute valid FFT bin range from modal frequency band configuration.
    /// @param fft_size FFT window size for bin calculation.
    /// @param sample_rate_hz Sampling rate [Hz].
    /// @return FftBinRange with valid bins or valid=false if range empty.
    [[nodiscard]] FftBinRange SelectFftBinRange(std::size_t fft_size, float sample_rate_hz) const noexcept;
    /// @brief Estimate natural frequency via FFT on signed gyro axis decay region.
    /// Applies Hann window, de-means (optional), pads to 512 or 1024, runs ESP-DSP FFT,
    /// and finds peak bin within the configured frequency band.
    /// @param axis Which gyro axis to analyze (dominant axis from ComputeDominantAxisSway).
    /// @param start Start sample index of decay region.
    /// @param count Number of samples in decay region.
    /// @return Natural frequency [Hz] or 0.0 if estimation fails.
    /// @see imu_algorithms/_extraction.py::extract_natural_frequency
    [[nodiscard]] float ComputeSignedAxisNaturalFrequency(DominantAxis axis, std::size_t start, std::size_t count) noexcept;
    /// @brief Estimate damping ratio via OLS log-fit on peak-hold envelope.
    ///
    /// Algorithm:
    /// 1. Build asymmetric peak-hold envelope: env[n] = max(gmag[n], alpha * env[n-1])
    ///    where alpha = exp(-2*pi*fc*dt), fc = 2 Hz.
    /// 2. Skip first ~1 cycle (transient).
    /// 3. Compute lower fit bound: max(4*baseline_noise, 0.03*peak_env).
    /// 4. OLS regression on ln(env): zeta = -slope / (2*pi*fn).
    /// 5. Classify confidence: high (r^2>0.90, 3+ cycles, quality==Reliable),
    ///    medium (r^2>0.70, amp drop), low (otherwise).
    ///
    /// @param start Start sample index of decay region.
    /// @param count Number of samples in decay region.
    /// @param natural_freq_hz Previously estimated natural frequency [Hz].
    /// @param quality Decay region quality from FindDecayOnsetTkeo.
    /// @return DampingFitResult with zeta and confidence string.
    /// @see imu_algorithms/_envelope.py::damping_from_envelope
    /// @see imu_algorithms/_envelope.py::envelope_peak_hold
    [[nodiscard]] DampingFitResult ComputePeakHoldDamping(std::size_t start, std::size_t count,
                                                           float natural_freq_hz,
                                                           DecayQuality quality) noexcept;
    /// @brief Run full post-hoc modal analysis on the current disturbance buffer.
    /// Calls FindDecayOnsetTkeo, ComputeDominantAxisSway, ComputeSignedAxisNaturalFrequency,
    /// and ComputePeakHoldDamping in sequence.
    /// @return EventAnalysisResult with natural frequency, damping ratio, and confidence.
    [[nodiscard]] EventAnalysisResult AnalyzeImuEvent() noexcept;
    /// @brief Copy a confidence string into a fixed-size char array.
    static void SetConfidence(std::array<char, MonitorResult::kDampingConfidenceMax>& dst,
                              const char* src) noexcept;

    void CheckFailureEvents() noexcept;
    void PublishFailure(FailureEvent event) noexcept;
    [[nodiscard]] bool InitAeSpectralAdc() noexcept;
    void ProcessAeSpectralWindow() noexcept;
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
    TaskHandle_t ae_spectral_task_handle_{nullptr};

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

    std::size_t disturbed_exit_debounce_counter_{0U};
    TkeoWindow tkeo_window_{};
    DspDisturbanceDetector dsp_detector_{};


    NodeState state_{NodeState::IDLE};

    float peak_gmag_{0.0f};

    std::array<float, kFftWindowSamples * 2U> fft_input_{};
    std::array<float, kFftWindowSamples / 2U> psd_accum_{};
    std::array<float, kStorageSamples> residual_scratch_{};
    bool fft_initialized_{false};

    void* adc_handle_{nullptr};
    bool adc_initialized_{false};
    AeSpectralDetector ae_spectral_detector_{};
    std::array<std::uint16_t, kAeSpectralMaxWindowSamples> ae_sample_window_{};
    std::array<float, kAeSpectralMaxWindowSamples * 2U> ae_fft_buffer_{};
    std::array<std::uint8_t, 256U> ae_adc_read_buffer_{};
    std::size_t ae_sample_count_{0U};
    std::uint32_t ae_spectral_windows_{0U};
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
