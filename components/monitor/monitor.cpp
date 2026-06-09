#include "monitor.hpp"

#include <cmath>
#include <cstdio>
#include <algorithm>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "dsps_fft2r.h"
#include "dsps_view.h"
#include "esp_timer.h"
#include "esp_event.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_attr.h"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

namespace monitor {

ESP_EVENT_DEFINE_BASE(MONITOR_EVENT_BASE);

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.2831853071795864769f;
static const char* kTag = "MONITOR";
constexpr std::size_t kTaskStackSize = 8192U;
constexpr UBaseType_t kTaskPriority = 5U;
constexpr BaseType_t kTaskCore = 1;

void MonitorTaskEntry(void* arg) noexcept {
    auto* self = static_cast<Monitor*>(arg);
    self->TaskLoop();
}

} // namespace

bool TkeoWindow::Push(float gmag, float& out_tkeo) noexcept {
    out_tkeo = 0.0f;
    if (count_ < samples_.size()) {
        samples_[count_] = gmag;
        ++count_;
        if (count_ < samples_.size()) {
            return false;
        }
    } else {
        samples_[0U] = samples_[1U];
        samples_[1U] = samples_[2U];
        samples_[2U] = gmag;
    }

    out_tkeo = (samples_[1U] * samples_[1U]) - (samples_[0U] * samples_[2U]);
    return true;
}

void TkeoWindow::Reset() noexcept {
    samples_ = {};
    count_ = 0U;
}

void DspDisturbanceDetector::Reset() noexcept {
    state_ = NodeState::IDLE;
    quiet_count_ = 0U;
}

NodeState DspDisturbanceDetector::Update(float gmag, float tkeo, const MonitorConfig& config) noexcept {
    if (state_ == NodeState::IDLE) {
        if ((tkeo > config.dsp_tkeo_high) || (gmag > config.dsp_gmag_onset_dps)) {
            state_ = NodeState::DISTURBED;
            quiet_count_ = 0U;
        }
        return state_;
    }

    if ((tkeo < config.dsp_tkeo_low) && (gmag < config.dsp_gmag_quiet_dps)) {
        if (quiet_count_ < config.dsp_quiet_debounce) {
            ++quiet_count_;
        }
        if (quiet_count_ >= config.dsp_quiet_debounce) {
            state_ = NodeState::IDLE;
            quiet_count_ = 0U;
        }
    } else {
        quiet_count_ = 0U;
    }

    return state_;
}

Monitor::Monitor(const sensor::Lsm6ds3::Config& imu_config,
                 const MonitorConfig& config) noexcept
    : imu_{imu_config},
      filter_{config.filter_alpha_base, config.filter_k_gain},
      config_{config} {}

bool Monitor::Init() noexcept {
    if (!imu_.init()) {
        return false;
    }

    if (!imu_.configure_motion_detection(0x09, 0x02, static_cast<std::uint8_t>(CONFIG_MONITOR_FREEFALL_THS),
                                        static_cast<std::uint8_t>(CONFIG_MONITOR_FF_DUR))) {
        return false;
    }

#if CONFIG_MONITOR_AE_MODE_GPIO
    if (config_.ae_gpio_pin < 0) {
        return false;
    }
    const gpio_num_t ae_gpio = static_cast<gpio_num_t>(config_.ae_gpio_pin);
    gpio_config_t io_cfg{};
    io_cfg.pin_bit_mask = (1ULL << static_cast<std::uint32_t>(ae_gpio));
    io_cfg.mode = GPIO_MODE_INPUT;
    io_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    io_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_cfg.intr_type = GPIO_INTR_POSEDGE;
    if (gpio_config(&io_cfg) != ESP_OK) {
        return false;
    }

    const esp_err_t isr_err = gpio_install_isr_service(0);
    if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
        return false;
    }

    if (gpio_isr_handler_add(ae_gpio, &Monitor::AeGpioIsr, this) != ESP_OK) {
        return false;
    }

    portENTER_CRITICAL(&ae_mux_);
    pending_ae_events_ = 0U;
    portEXIT_CRITICAL(&ae_mux_);
#else
    if (config_.ae_adc_channel < 0) {
        return false;
    }
    adc_oneshot_unit_init_cfg_t init_cfg{};
    init_cfg.unit_id = ADC_UNIT_1;
    adc_oneshot_unit_handle_t handle = nullptr;
    if (adc_oneshot_new_unit(&init_cfg, &handle) != ESP_OK) {
        return false;
    }
    adc_oneshot_chan_cfg_t chan_cfg{};
    chan_cfg.atten = ADC_ATTEN_DB_11;
    chan_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_oneshot_config_channel(handle,
                                   static_cast<adc_channel_t>(config_.ae_adc_channel),
                                   &chan_cfg) != ESP_OK) {
        adc_oneshot_del_unit(handle);
        return false;
    }
    adc_handle_ = handle;
    adc_initialized_ = true;
#endif

    const esp_err_t err = dsps_fft2r_init_fc32(nullptr, static_cast<int>(kFftWindowSamples));
    if (err != ESP_OK) {
        return false;
    }

    fft_initialized_ = true;

#if CONFIG_MONITOR_IMU_CALIBRATION
    {
        esp_err_t nvs_err = nvs_open("calib", NVS_READONLY, &calib_nvs_handle_);
        if (nvs_err == ESP_OK) {
            calibration::Calibration::ReadBiases(calib_nvs_handle_, calib_bias_);
            nvs_close(calib_nvs_handle_);
            calib_nvs_handle_ = 0;
        }
    }
#endif

#if CONFIG_MONITOR_TARE_ENABLE
    taring_complete_ = false;
#else
    taring_complete_ = true;
#endif
    roll_offset_ = 0.0f;
    pitch_offset_ = 0.0f;
    roll_tare_sum_ = 0.0f;
    pitch_tare_sum_ = 0.0f;
    tare_samples_accumulated_ = 0U;
    tare_settle_accumulated_ = 0U;

    return true;
}

bool Monitor::Start() noexcept {
    const BaseType_t ret = xTaskCreatePinnedToCore(
        MonitorTaskEntry,
        "monitor_task",
        kTaskStackSize,
        this,
        kTaskPriority,
        &task_handle_,
        kTaskCore);
    if (ret != pdPASS) {
        ESP_LOGE(kTag, "Failed to create monitor_task");
        return false;
    }
    ESP_LOGI(kTag, "monitor_task started on core %d, priority %u", kTaskCore, kTaskPriority);
    return true;
}

void Monitor::TaskLoop() noexcept {
    const float dt_s = 1.0f / static_cast<float>(CONFIG_MONITOR_IMU_RATE_HZ);
    const std::uint32_t rate_hz = static_cast<std::uint32_t>(CONFIG_MONITOR_IMU_RATE_HZ);
    const std::uint32_t period_ms = (1000U + rate_hz - 1U) / rate_hz;
    const TickType_t period_ticks = pdMS_TO_TICKS(period_ms);
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        if (!Update(dt_s)) {
            ESP_LOGW(kTag, "Monitor update failed");
        }
        vTaskDelayUntil(&last_wake, period_ticks);
    }
}

bool Monitor::Update(float dt_s) noexcept {
    sensor::lsm6ds3::Value gyro{};
    sensor::lsm6ds3::Value accel{};
    if (!ReadImu(gyro, accel)) {
        return false;
    }

    const float calib_ax = accel.x - calib_bias_.ax;
    const float calib_ay = accel.y - calib_bias_.ay;
    const float calib_az = accel.z - calib_bias_.az;
    const float calib_gx = gyro.x - calib_bias_.gx;
    const float calib_gy = gyro.y - calib_bias_.gy;
    const float calib_gz = gyro.z - calib_bias_.gz;

    const std::array<float, 3> accel_vec{calib_ax, calib_ay, calib_az};
    const std::array<float, 3> gyro_vec{calib_gx, calib_gy, calib_gz};
    hpf_.update(calib_ax, calib_ay, calib_az);
    filter_.update(accel_vec, gyro_vec, dt_s);
    const float gmag = std::sqrt((calib_gx * calib_gx) + (calib_gy * calib_gy) + (calib_gz * calib_gz));
    float tkeo = 0.0f;
    static_cast<void>(tkeo_window_.Push(gmag, tkeo));

    float current_roll = filter_.roll();
    float current_pitch = filter_.pitch();

#if CONFIG_MONITOR_TARE_ENABLE
    if (taring_complete_) {
        current_roll -= roll_offset_;
        current_pitch -= pitch_offset_;
    } else {
        if (tare_settle_accumulated_ < static_cast<std::size_t>(CONFIG_MONITOR_TARE_SETTLE_SAMPLES)) {
            ++tare_settle_accumulated_;
        } else {
            roll_tare_sum_ += current_roll;
            pitch_tare_sum_ += current_pitch;
            ++tare_samples_accumulated_;
            if (tare_samples_accumulated_ >= static_cast<std::size_t>(CONFIG_MONITOR_TARE_SAMPLES)) {
                roll_offset_ = roll_tare_sum_ / static_cast<float>(CONFIG_MONITOR_TARE_SAMPLES);
                pitch_offset_ = pitch_tare_sum_ / static_cast<float>(CONFIG_MONITOR_TARE_SAMPLES);
                taring_complete_ = true;
                ESP_LOGI(kTag, "Taring complete: roll_offset=%.3f pitch_offset=%.3f over %d samples",
                         roll_offset_, pitch_offset_, CONFIG_MONITOR_TARE_SAMPLES);

                for (std::size_t i = 0U; i < write_index_; ++i) {
                    roll_history_[i] -= roll_offset_;
                    pitch_history_[i] -= pitch_offset_;
                }
                for (std::size_t i = 0U; i < stream_count_; ++i) {
                    stream_samples_[i].roll -= roll_offset_;
                    stream_samples_[i].pitch -= pitch_offset_;
                }

                current_roll -= roll_offset_;
                current_pitch -= pitch_offset_;
            }
        }
    }
#endif

    PushSample(current_roll, current_pitch, gmag, tkeo);

    ESP_LOGD(kTag, "Stream: ax=%.3f ay=%.3f az=%.3f gx=%.3f gy=%.3f gz=%.3f r=%.3f p=%.3f",
             calib_ax, calib_ay, calib_az, calib_gx, calib_gy, calib_gz, current_roll, current_pitch);

    StreamSample sample{};
    sample.accel_x = calib_ax;
    sample.accel_y = calib_ay;
    sample.accel_z = calib_az;
    sample.gyro_x = calib_gx;
    sample.gyro_y = calib_gy;
    sample.gyro_z = calib_gz;
    sample.roll = current_roll;
    sample.pitch = current_pitch;
    sample.timestamp_us = static_cast<std::uint64_t>(esp_timer_get_time());

    {
        std::lock_guard<std::mutex> lock(mutex_);
        stream_samples_[stream_write_index_] = sample;
        stream_write_index_ = (stream_write_index_ + 1U) % kMaxStreamSamples;
        if (stream_count_ < kMaxStreamSamples) {
            ++stream_count_;
        }
    }

    CheckFailureEvents();

    if ((sample_count_ >= kStorageSamples) && (write_index_ == 0U)) {
        if (state_ == NodeState::IDLE) {
            MonitorResult result{};
            if (ComputeStats(result)) {
                idle_5min_roll_var_ = result.roll_variance;
                idle_5min_pitch_var_ = result.pitch_variance;
                has_baseline_variance_ = true;
                
                // For IDLE, we only want to publish the tilt statistics (mean, variance).
                // Zero out the rest to avoid DC bias in dashboard/logger.
                result.natural_freq_hz = 0.0f;
                result.natural_freq_roll_hz = 0.0f;
                result.natural_freq_pitch_hz = 0.0f;
                result.roll_damping_ratio = 0.0f;
                result.pitch_damping_ratio = 0.0f;
                result.roll_sway_pp_max = 0.0f;
                result.roll_sway_pp_mean = 0.0f;
                result.pitch_sway_pp_max = 0.0f;
                result.pitch_sway_pp_mean = 0.0f;
                result.state = NodeState::IDLE;
                
                const esp_err_t post_err = esp_event_post(MONITOR_EVENT_BASE,
                                                          MONITOR_EVENT_RESULT,
                                                          &result,
                                                          sizeof(result),
                                                          0);
                if (post_err != ESP_OK) {
                    ++dropped_result_events_;
                    ESP_LOGW(kTag, "IDLE result event post failed: %s", esp_err_to_name(post_err));
                }
                
                ESP_LOGI(kTag, "IDLE baseline updated. roll_var=%.3f pitch_var=%.3f",
                         idle_5min_roll_var_, idle_5min_pitch_var_);
            }
        }
    }

    return true;
}

bool Monitor::ReadImuSample(sensor::lsm6ds3::Value& gyro,
                            sensor::lsm6ds3::Value& accel) noexcept {
    return ReadImu(gyro, accel);
}

bool Monitor::ReadImu(sensor::lsm6ds3::Value& gyro,
                      sensor::lsm6ds3::Value& accel) noexcept {
    return imu_.read_accel_gyro(gyro, accel);
}

void Monitor::PushSample(float roll, float pitch, float gmag, float tkeo) noexcept {
    bool should_compute = false;
    NodeState pub_state = NodeState::IDLE;
    bool is_exit = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        const NodeState old_detector_state = dsp_detector_.State();
        const NodeState new_detector_state = dsp_detector_.Update(gmag, tkeo, config_);

        if (short_sample_count_ == kShortBufferSamples) {
            const float old_roll = roll_short_[short_write_index_];
            const float old_pitch = pitch_short_[short_write_index_];
            roll_short_sum_ -= old_roll;
            roll_short_sq_sum_ -= (old_roll * old_roll);
            pitch_short_sum_ -= old_pitch;
            pitch_short_sq_sum_ -= (old_pitch * old_pitch);
        } else {
            ++short_sample_count_;
        }

        roll_short_[short_write_index_] = roll;
        pitch_short_[short_write_index_] = pitch;
        gmag_short_[short_write_index_] = gmag;
        roll_short_sum_ += roll;
        roll_short_sq_sum_ += (roll * roll);
        pitch_short_sum_ += pitch;
        pitch_short_sq_sum_ += (pitch * pitch);

        short_write_index_ = (short_write_index_ + 1U) % kShortBufferSamples;

        if (state_ == NodeState::IDLE) {
            roll_history_[write_index_] = roll;
            pitch_history_[write_index_] = pitch;
            gmag_history_[write_index_] = gmag;
            write_index_ = (write_index_ + 1U) % kStorageSamples;
            if (sample_count_ < kStorageSamples) {
                ++sample_count_;
            }

            if ((old_detector_state == NodeState::IDLE) && (new_detector_state == NodeState::DISTURBED)) {
                state_ = NodeState::DISTURBED;

                write_index_ = 0U;
                sample_count_ = 0U;

                const std::size_t count = short_sample_count_;
                for (std::size_t i = 0U; i < count; ++i) {
                    const std::size_t idx = (short_write_index_ + kShortBufferSamples - count + i) % kShortBufferSamples;
                    roll_history_[i] = roll_short_[idx];
                    pitch_history_[i] = pitch_short_[idx];
                    gmag_history_[i] = gmag_short_[idx];
                }
                write_index_ = count;
                sample_count_ = count;

                disturbed_exit_debounce_counter_ = 0U;

                ESP_LOGI(kTag, "Transition: IDLE -> DISTURBED (gmag=%.6f tkeo=%.6f)",
                         static_cast<double>(gmag), static_cast<double>(tkeo));
            }
        } else if (state_ == NodeState::DISTURBED) {
            roll_history_[write_index_] = roll;
            pitch_history_[write_index_] = pitch;
            gmag_history_[write_index_] = gmag;
            write_index_ = (write_index_ + 1U) % kStorageSamples;
            if (sample_count_ < kStorageSamples) {
                ++sample_count_;
            }

            bool transitioned = false;
            disturbed_exit_debounce_counter_ = dsp_detector_.QuietCount();
            if ((old_detector_state == NodeState::DISTURBED) && (new_detector_state == NodeState::IDLE)) {
                state_ = NodeState::IDLE;
                transitioned = true;

                ESP_LOGI(kTag, "Transition: DISTURBED -> IDLE (gmag=%.6f tkeo=%.6f)",
                         static_cast<double>(gmag), static_cast<double>(tkeo));

                should_compute = true;
                pub_state = NodeState::DISTURBED;
                is_exit = true;
            }

            if (!transitioned && sample_count_ >= (kStorageSamples - static_cast<std::size_t>(CONFIG_MONITOR_N_DPAD))) {
                ESP_LOGI(kTag, "DISTURBED Buffer Refreshed");

                write_index_ = 0U;
                sample_count_ = 0U;

                const std::size_t count = short_sample_count_;
                for (std::size_t i = 0U; i < count; ++i) {
                    const std::size_t idx = (short_write_index_ + kShortBufferSamples - count + i) % kShortBufferSamples;
                    roll_history_[i] = roll_short_[idx];
                    pitch_history_[i] = pitch_short_[idx];
                    gmag_history_[i] = gmag_short_[idx];
                }
                write_index_ = count;
                sample_count_ = count;

                should_compute = true;
                pub_state = NodeState::DISTURBED;
                is_exit = false;
            }
        }
    } // mutex_ released here

    if (should_compute) {
        static_cast<void>(ComputeAndPublish(pub_state, is_exit));
    }
}

bool Monitor::ComputeAndPublish(NodeState pub_state, bool is_exit) noexcept {
    if (!fft_initialized_) {
        return false;
    }

    MonitorResult result{};
    if (!ComputeStats(result)) {
        return false;
    }

    result.state = pub_state;

    if (pub_state == NodeState::DISTURBED) {
        static_cast<void>(ComputeSwayAndDamping(result));
        if (is_exit) {
#if CONFIG_MONITOR_DEBUG_DUMP
            const std::int64_t modal_start_us = esp_timer_get_time();
#endif
            AnalyzeModalAxis(roll_history_, roll_modal_scratch_);
            AnalyzeModalAxis(pitch_history_, pitch_modal_scratch_);

            result.natural_freq_hz = ComputeGmagNaturalFrequency();
            result.natural_freq_roll_hz = result.natural_freq_hz;
            result.natural_freq_pitch_hz = result.natural_freq_hz;

            result.roll_damping_ratio = ComputeDampingRegression(roll_modal_scratch_.pair_envelope,
                                                                 result.natural_freq_hz);
            result.pitch_damping_ratio = ComputeDampingRegression(pitch_modal_scratch_.pair_envelope,
                                                                  result.natural_freq_hz);

#if CONFIG_MONITOR_DEBUG_DUMP
            const std::int64_t modal_elapsed_us = esp_timer_get_time() - modal_start_us;
            ESP_LOGI(kTag, "Modal analysis elapsed=%lldus roll_pairs=%u pitch_pairs=%u",
                     static_cast<long long>(modal_elapsed_us),
                     static_cast<unsigned>(roll_modal_scratch_.centerline_pairs.count),
                     static_cast<unsigned>(pitch_modal_scratch_.centerline_pairs.count));
            DumpDebugToSD(roll_modal_scratch_, pitch_modal_scratch_,
                           result.natural_freq_roll_hz, result.natural_freq_pitch_hz,
                           result.roll_damping_ratio, result.pitch_damping_ratio,
                           modal_elapsed_us);
#endif
        } else {
            result.natural_freq_roll_hz = 0.0f;
            result.natural_freq_pitch_hz = 0.0f;
            result.natural_freq_hz = 0.0f;
            result.roll_damping_ratio = 0.0f;
            result.pitch_damping_ratio = 0.0f;
        }
    }

    ESP_LOGD(kTag,
             "roll_mean=%.3f roll_var=%.3f pitch_mean=%.3f pitch_var=%.3f "
             "roll_pp_max=%.3f roll_pp_mean=%.3f pitch_pp_max=%.3f pitch_pp_mean=%.3f "
             "roll_zeta=%.4f pitch_zeta=%.4f freq=%.3fHz freq_roll=%.3fHz freq_pitch=%.3fHz samples=%u ts_us=%llu state=%d",
             result.roll_mean,
             result.roll_variance,
             result.pitch_mean,
             result.pitch_variance,
             result.roll_sway_pp_max,
             result.roll_sway_pp_mean,
             result.pitch_sway_pp_max,
             result.pitch_sway_pp_mean,
             result.roll_damping_ratio,
             result.pitch_damping_ratio,
             result.natural_freq_hz,
             result.natural_freq_roll_hz,
             result.natural_freq_pitch_hz,
             static_cast<unsigned>(result.sample_count),
             static_cast<unsigned long long>(result.timestamp_us),
             static_cast<int>(result.state));

    const esp_err_t post_err = esp_event_post(MONITOR_EVENT_BASE,
                                              MONITOR_EVENT_RESULT,
                                              &result,
                                              sizeof(result),
                                              0);
    if (post_err != ESP_OK) {
        ++dropped_result_events_;
        ESP_LOGW(kTag, "Result event post failed: %s", esp_err_to_name(post_err));
        return false;
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


Monitor::DecayRegion Monitor::FindDecayRegion(const std::array<float, kStorageSamples>& data, PeakList& out_peaks) const noexcept {
    DecayRegion region{};
    out_peaks.count = 0U;

    const std::size_t count = BufferSize();
    if (count < 3U) {
        return region;
    }

    const float min_amp = config_.peak_min_amplitude_deg;
    const std::size_t min_spacing = config_.peak_min_spacing;

    // 1. Detect all peaks in the buffer and find the global maximum
    PeakList all_peaks{};
    float max_abs = 0.0f;
    std::size_t max_idx = 0U;
    bool has_max = false;

    std::size_t last_ext_idx = 0U;
    bool has_last_ext = false;

    for (std::size_t i = 1U; i + 1U < count; ++i) {
        const std::size_t idx_prev = PhysicalIndex(i - 1U);
        const std::size_t idx_curr = PhysicalIndex(i);
        const std::size_t idx_next = PhysicalIndex(i + 1U);

        const float prev = data[idx_prev];
        const float curr = data[idx_curr];
        const float next = data[idx_next];

        const float abs_val = std::fabs(curr);

        const bool is_peak = (curr > prev) && (curr > next) && (abs_val >= min_amp);
        const bool is_trough = (curr < prev) && (curr < next) && (abs_val >= min_amp);

        if (!(is_peak || is_trough)) {
            continue;
        }

        if (has_last_ext && ((i - last_ext_idx) < min_spacing)) {
            continue;
        }

        if (all_peaks.count < PeakList::kMaxPeaks) {
            all_peaks.amplitudes[all_peaks.count] = abs_val;
            all_peaks.times[all_peaks.count] = static_cast<float>(i) / static_cast<float>(CONFIG_MONITOR_IMU_RATE_HZ);
            all_peaks.count++;
        }

        if (!has_max || abs_val > max_abs) {
            max_abs = abs_val;
            max_idx = all_peaks.count - 1U;
            has_max = true;
        }

        last_ext_idx = i;
        has_last_ext = true;
    }

    if (!has_max || all_peaks.count == 0U) {
        return region;
    }

    // 2. Track declining envelope from max_idx forward
    std::size_t start_logical = static_cast<std::size_t>(all_peaks.times[max_idx] * static_cast<float>(CONFIG_MONITOR_IMU_RATE_HZ));
    region.start_index = PhysicalIndex(start_logical);
    region.count = count - start_logical;

    float last_amp = all_peaks.amplitudes[max_idx];
    out_peaks.amplitudes[out_peaks.count] = last_amp;
    out_peaks.times[out_peaks.count] = all_peaks.times[max_idx];
    out_peaks.count++;

    for (std::size_t i = max_idx + 1U; i < all_peaks.count; ++i) {
        const float amp = all_peaks.amplitudes[i];
        if (amp > last_amp) {
            break;
        }
        out_peaks.amplitudes[out_peaks.count] = amp;
        out_peaks.times[out_peaks.count] = all_peaks.times[i];
        out_peaks.count++;
        last_amp = amp;
    }

    return region;
}

float Monitor::ComputeDampingRegression(const PeakList& peaks, float natural_freq_hz) const noexcept {
    if (peaks.count < 4U || natural_freq_hz <= 0.0f) {
        return 0.0f;
    }

    const float wn = kTwoPi * natural_freq_hz;
    const std::size_t n = peaks.count;

    float sum_t = 0.0f;
    float sum_y = 0.0f;
    float sum_ty = 0.0f;
    float sum_t2 = 0.0f;

    for (std::size_t i = 0U; i < n; ++i) {
        const float t = peaks.times[i];
        const float y = std::log(peaks.amplitudes[i]);

        sum_t += t;
        sum_y += y;
        sum_ty += t * y;
        sum_t2 += t * t;
    }

    const float fn = static_cast<float>(n);
    const float denominator = (fn * sum_t2) - (sum_t * sum_t);

    if (denominator <= 0.0f) {
        return 0.0f;
    }

    const float slope = ((fn * sum_ty) - (sum_t * sum_y)) / denominator;

    if (slope >= 0.0f) {
        return 0.0f;
    }

    return std::fabs(slope) / wn;
}

bool Monitor::DetectRawExtrema(const std::array<float, kStorageSamples>& data,
                               std::size_t start_logical,
                               std::size_t count,
                               ExtremaList& out_extrema) const noexcept {
    out_extrema.count = 0U;
    const std::size_t buffer_count = BufferSize();
    if ((count < 3U) || (start_logical >= buffer_count)) {
        return false;
    }

    const std::size_t end_logical = std::min(start_logical + count, buffer_count);
    std::size_t last_ext_idx = 0U;
    bool has_last_ext = false;

    for (std::size_t i = start_logical + 1U; i + 1U < end_logical; ++i) {
        const float prev = data[PhysicalIndex(i - 1U)];
        const float curr = data[PhysicalIndex(i)];
        const float next = data[PhysicalIndex(i + 1U)];

        const bool is_peak = (curr > prev) && (curr > next);
        const bool is_trough = (curr < prev) && (curr < next);
        if (!(is_peak || is_trough)) {
            continue;
        }

        if (has_last_ext && ((i - last_ext_idx) < config_.peak_min_spacing)) {
            continue;
        }

        if (out_extrema.count >= ExtremaList::kMaxExtrema) {
            break;
        }

        ExtremaPoint& point = out_extrema.points[out_extrema.count];
        point.logical_index = i;
        point.value = curr;
        point.kind = is_peak ? ExtremaKind::Peak : ExtremaKind::Trough;
        ++out_extrema.count;
        last_ext_idx = i;
        has_last_ext = true;
    }

    return out_extrema.count > 0U;
}

bool Monitor::CollapseExtremaLobes(const ExtremaList& raw_extrema,
                                   ExtremaList& out_extrema) const noexcept {
    out_extrema.count = 0U;
    if (raw_extrema.count == 0U) {
        return false;
    }

    ExtremaPoint active = raw_extrema.points[0U];
    for (std::size_t i = 1U; i < raw_extrema.count; ++i) {
        const ExtremaPoint& curr = raw_extrema.points[i];
        if (curr.kind == active.kind) {
            const bool stronger_peak = (curr.kind == ExtremaKind::Peak) && (curr.value > active.value);
            const bool stronger_trough = (curr.kind == ExtremaKind::Trough) && (curr.value < active.value);
            if (stronger_peak || stronger_trough) {
                active = curr;
            }
            continue;
        }

        if (std::fabs(curr.value - active.value) < config_.centerline_lobe_reversal_deg) {
            continue;
        }

        if (out_extrema.count >= ExtremaList::kMaxExtrema) {
            break;
        }
        out_extrema.points[out_extrema.count] = active;
        ++out_extrema.count;
        active = curr;
    }

    if (out_extrema.count < ExtremaList::kMaxExtrema) {
        out_extrema.points[out_extrema.count] = active;
        ++out_extrema.count;
    }

    return out_extrema.count > 0U;
}

bool Monitor::BuildCenterlinePairs(const ExtremaList& extrema,
                                   CenterlinePairList& out_pairs) const noexcept {
    out_pairs.count = 0U;
    if (extrema.count < 2U) {
        return false;
    }

    const float sample_rate_hz = static_cast<float>(CONFIG_MONITOR_IMU_RATE_HZ);
    for (std::size_t i = 0U; i + 1U < extrema.count; ++i) {
        const ExtremaPoint& left = extrema.points[i];
        const ExtremaPoint& right = extrema.points[i + 1U];
        if (left.kind == right.kind) {
            continue;
        }

        const float amplitude = 0.5f * std::fabs(right.value - left.value);
        if (amplitude < config_.centerline_min_amplitude_deg) {
            continue;
        }

        if (out_pairs.count >= CenterlinePairList::kMaxPairs) {
            break;
        }

        CenterlinePair& pair = out_pairs.pairs[out_pairs.count];
        pair.center_logical_index = (left.logical_index + right.logical_index) / 2U;
        pair.center_value = 0.5f * (left.value + right.value);
        pair.amplitude = amplitude;
        pair.time_s = static_cast<float>(pair.center_logical_index) / sample_rate_hz;
        ++out_pairs.count;
    }

    return out_pairs.count > 0U;
}

bool Monitor::SubtractCenterline(const std::array<float, kStorageSamples>& data,
                                 std::size_t start_logical,
                                 std::size_t count,
                                 const CenterlinePairList& pairs) noexcept {
    if ((count == 0U) || (count > kStorageSamples) || (pairs.count < 2U)) {
        return false;
    }

    std::size_t pair_idx = 0U;
    for (std::size_t i = 0U; i < count; ++i) {
        const std::size_t logical = start_logical + i;
        while ((pair_idx + 1U) < pairs.count &&
               logical > pairs.pairs[pair_idx + 1U].center_logical_index) {
            ++pair_idx;
        }

        float center = pairs.pairs[pair_idx].center_value;
        if ((pair_idx + 1U) < pairs.count &&
            logical >= pairs.pairs[pair_idx].center_logical_index) {
            const CenterlinePair& left = pairs.pairs[pair_idx];
            const CenterlinePair& right = pairs.pairs[pair_idx + 1U];
            const std::size_t span = right.center_logical_index - left.center_logical_index;
            if (span > 0U) {
                const float frac = static_cast<float>(logical - left.center_logical_index) /
                                   static_cast<float>(span);
                center = left.center_value + (right.center_value - left.center_value) * frac;
            }
        } else if (logical > pairs.pairs[pairs.count - 1U].center_logical_index) {
            center = pairs.pairs[pairs.count - 1U].center_value;
        }

        residual_scratch_[i] = data[PhysicalIndex(logical)] - center;
    }

    return true;
}

bool Monitor::SelectPairEnvelope(const CenterlinePairList& pairs,
                                 PeakList& out_envelope) const noexcept {
    out_envelope.count = 0U;
    if (pairs.count == 0U) {
        return false;
    }

    std::size_t max_idx = 0U;
    float max_amp = pairs.pairs[0U].amplitude;
    for (std::size_t i = 1U; i < pairs.count; ++i) {
        if (pairs.pairs[i].amplitude > max_amp) {
            max_amp = pairs.pairs[i].amplitude;
            max_idx = i;
        }
    }

    float last_amp = max_amp;
    for (std::size_t i = max_idx; i < pairs.count && out_envelope.count < PeakList::kMaxPeaks; ++i) {
        const float amp = pairs.pairs[i].amplitude;
        if (amp > last_amp) {
            break;
        }
        out_envelope.amplitudes[out_envelope.count] = amp;
        out_envelope.times[out_envelope.count] = pairs.pairs[i].time_s;
        ++out_envelope.count;
        last_amp = amp;
    }

    return out_envelope.count > 0U;
}

Monitor::FftBinRange Monitor::SelectFftBinRange(std::size_t fft_size,
                                                float sample_rate_hz) const noexcept {
    FftBinRange range{};
    if ((fft_size < 4U) || (sample_rate_hz <= 0.0f) ||
        (config_.modal_freq_max_hz < config_.modal_freq_min_hz)) {
        return range;
    }

    const float fft_size_f = static_cast<float>(fft_size);
    std::size_t min_bin = static_cast<std::size_t>(
        std::ceil((config_.modal_freq_min_hz * fft_size_f) / sample_rate_hz));
    std::size_t max_bin = static_cast<std::size_t>(
        std::floor((config_.modal_freq_max_hz * fft_size_f) / sample_rate_hz));

    const std::size_t max_valid_bin = (fft_size / 2U) - 1U;
    min_bin = std::max<std::size_t>(min_bin, 1U);
    max_bin = std::min(max_bin, max_valid_bin);
    if (min_bin > max_bin) {
        return range;
    }

    range.min_bin = min_bin;
    range.max_bin = max_bin;
    range.valid = true;
    return range;
}

float Monitor::ComputeResidualNaturalFrequency(const float* residual, std::size_t count) noexcept {
    if ((residual == nullptr) || (count == 0U)) {
        return 0.0f;
    }

    const float sample_rate_hz = static_cast<float>(CONFIG_MONITOR_IMU_RATE_HZ);
    if (count < kFftWindowSamples) {
        const std::size_t fft_size = (count < 512U) ? 512U : 1024U;
        const FftBinRange range = SelectFftBinRange(fft_size, sample_rate_hz);
        if (!range.valid) {
            return 0.0f;
        }

        std::fill(fft_input_.begin(), fft_input_.begin() + (fft_size * 2U), 0.0f);

        float mean = 0.0f;
#if CONFIG_MONITOR_FFT_DE_MEAN_ENABLE
        float sum = 0.0f;
        for (std::size_t i = 0U; i < count; ++i) {
            sum += residual[i];
        }
        mean = sum / static_cast<float>(count);
#endif

        for (std::size_t i = 0U; i < count; ++i) {
            const float val = residual[i] - mean;
            const float window = (count > 1U) ?
                (0.5f - 0.5f * std::cos(kTwoPi * static_cast<float>(i) / static_cast<float>(count - 1U))) :
                1.0f;
            fft_input_[2U * i] = val * window;
            fft_input_[2U * i + 1U] = 0.0f;
        }

        if (dsps_fft2r_fc32(fft_input_.data(), static_cast<int>(fft_size)) != ESP_OK) {
            return 0.0f;
        }
        if (dsps_bit_rev_fc32(fft_input_.data(), static_cast<int>(fft_size)) != ESP_OK) {
            return 0.0f;
        }

        float max_val = 0.0f;
        std::size_t max_bin = 0U;
        for (std::size_t bin = range.min_bin; bin <= range.max_bin; ++bin) {
            const float real = fft_input_[2U * bin];
            const float imag = fft_input_[2U * bin + 1U];
            const float power = (real * real) + (imag * imag);
            if (power > max_val) {
                max_val = power;
                max_bin = bin;
            }
            if (fft_size == 512U) {
                psd_accum_[2U * bin] += power;
                psd_accum_[2U * bin + 1U] += power;
            } else {
                psd_accum_[bin] += power;
            }
        }

        return (max_bin == 0U) ? 0.0f :
            ((static_cast<float>(max_bin) * sample_rate_hz) / static_cast<float>(fft_size));
    }

    const FftBinRange range = SelectFftBinRange(kFftWindowSamples, sample_rate_hz);
    if (!range.valid) {
        return 0.0f;
    }

    const std::size_t step = kFftWindowSamples - kFftOverlapSamples;
    const std::size_t segments = ((count - kFftWindowSamples) / step) + 1U;
    const std::size_t span = kFftWindowSamples + (segments - 1U) * step;
    const std::size_t base_start = count - span;

    std::array<float, kFftWindowSamples / 2U> local_psd{};
    for (std::size_t seg = 0U; seg < segments; ++seg) {
        const std::size_t seg_start = base_start + (seg * step);

        float mean = 0.0f;
#if CONFIG_MONITOR_FFT_DE_MEAN_ENABLE
        float sum = 0.0f;
        for (std::size_t i = 0U; i < kFftWindowSamples; ++i) {
            sum += residual[seg_start + i];
        }
        mean = sum / static_cast<float>(kFftWindowSamples);
#endif

        for (std::size_t i = 0U; i < kFftWindowSamples; ++i) {
            const float val = residual[seg_start + i] - mean;
            const float window = 0.5f - 0.5f *
                std::cos(kTwoPi * static_cast<float>(i) / static_cast<float>(kFftWindowSamples - 1U));
            fft_input_[2U * i] = val * window;
            fft_input_[2U * i + 1U] = 0.0f;
        }

        if (dsps_fft2r_fc32(fft_input_.data(), static_cast<int>(kFftWindowSamples)) != ESP_OK) {
            return 0.0f;
        }
        if (dsps_bit_rev_fc32(fft_input_.data(), static_cast<int>(kFftWindowSamples)) != ESP_OK) {
            return 0.0f;
        }

        for (std::size_t bin = range.min_bin; bin <= range.max_bin; ++bin) {
            const float real = fft_input_[2U * bin];
            const float imag = fft_input_[2U * bin + 1U];
            const float power = (real * real) + (imag * imag);
            local_psd[bin] += power;
            psd_accum_[bin] += power;
        }
    }

    const float norm = 1.0f / static_cast<float>(segments);
    float max_val = 0.0f;
    std::size_t max_bin = 0U;
    for (std::size_t bin = range.min_bin; bin <= range.max_bin; ++bin) {
        local_psd[bin] *= norm;
        if (local_psd[bin] > max_val) {
            max_val = local_psd[bin];
            max_bin = bin;
        }
    }

    return (max_bin == 0U) ? 0.0f :
        ((static_cast<float>(max_bin) * sample_rate_hz) / static_cast<float>(kFftWindowSamples));
}

void Monitor::AnalyzeModalAxis(const std::array<float, kStorageSamples>& data, ModalAxisResult& out) noexcept {
    out = ModalAxisResult{};
    const std::size_t count = BufferSize();
    if (count < 3U) {
        return;
    }

    static_cast<void>(DetectRawExtrema(data, 0U, count, out.raw_extrema));
    static_cast<void>(CollapseExtremaLobes(out.raw_extrema, out.collapsed_extrema));
    static_cast<void>(BuildCenterlinePairs(out.collapsed_extrema, out.centerline_pairs));
    static_cast<void>(SelectPairEnvelope(out.centerline_pairs, out.pair_envelope));

    if (out.pair_envelope.count == 0U || out.centerline_pairs.count < 2U) {
        return;
    }

    const float first_time_s = out.pair_envelope.times[0U];
    const std::size_t start_logical = static_cast<std::size_t>(
        first_time_s * static_cast<float>(CONFIG_MONITOR_IMU_RATE_HZ));
    if (start_logical >= count) {
        return;
    }

    out.decay.start_index = start_logical;
    out.decay.count = count - start_logical;
    if (!SubtractCenterline(data, out.decay.start_index, out.decay.count, out.centerline_pairs)) {
        return;
    }

    out.residual_count = out.decay.count;
    out.natural_freq_hz = ComputeResidualNaturalFrequency(residual_scratch_.data(), out.residual_count);
    out.damping_ratio = ComputeDampingRegression(out.pair_envelope, out.natural_freq_hz);
}

float Monitor::ComputeAxisNaturalFrequency(const std::array<float, kStorageSamples>& history, std::size_t start_phys_idx, std::size_t count) noexcept {
    if (count == 0U) {
        return 0.0f;
    }

    const float sample_rate_hz = static_cast<float>(CONFIG_MONITOR_IMU_RATE_HZ);

    if (count < kFftWindowSamples) {
        const std::size_t fft_size = (count < 512U) ? 512U : 1024U;
        const FftBinRange range = SelectFftBinRange(fft_size, sample_rate_hz);
        if (!range.valid) {
            return 0.0f;
        }

        std::fill(fft_input_.begin(), fft_input_.begin() + (fft_size * 2U), 0.0f);

        float mean = 0.0f;
#if CONFIG_MONITOR_FFT_DE_MEAN_ENABLE
        float sum = 0.0f;
        for (std::size_t i = 0U; i < count; ++i) {
            const std::size_t idx = (start_phys_idx + i) % kStorageSamples;
            const float val = history[idx];
            fft_input_[2U * i] = val;
            sum += val;
        }
        mean = sum / static_cast<float>(count);
#endif

        for (std::size_t i = 0U; i < count; ++i) {
            float val = 0.0f;
#if CONFIG_MONITOR_FFT_DE_MEAN_ENABLE
            val = fft_input_[2U * i] - mean;
#else
            const std::size_t idx = (start_phys_idx + i) % kStorageSamples;
            val = history[idx];
#endif

            const float window = (count > 1U) ? (0.5f - 0.5f * std::cos(kTwoPi * static_cast<float>(i) / static_cast<float>(count - 1U))) : 1.0f;
            fft_input_[2U * i] = val * window;
            fft_input_[2U * i + 1U] = 0.0f;
        }

        if (dsps_fft2r_fc32(fft_input_.data(), static_cast<int>(fft_size)) != ESP_OK) {
            return 0.0f;
        }
        if (dsps_bit_rev_fc32(fft_input_.data(), static_cast<int>(fft_size)) != ESP_OK) {
            return 0.0f;
        }

        float max_val = 0.0f;
        std::size_t max_bin = 0U;

        for (std::size_t bin = range.min_bin; bin <= range.max_bin; ++bin) {
            const float real = fft_input_[2U * bin];
            const float imag = fft_input_[2U * bin + 1U];
            const float power = (real * real) + (imag * imag);

            if (power > max_val) {
                max_val = power;
                max_bin = bin;
            }

            if (fft_size == 512U) {
                psd_accum_[2U * bin] += power;
                psd_accum_[2U * bin + 1U] += power;
            } else {
                psd_accum_[bin] += power;
            }
        }

        return (max_bin == 0U) ? 0.0f :
            ((static_cast<float>(max_bin) * sample_rate_hz) / static_cast<float>(fft_size));
    }

    const FftBinRange range = SelectFftBinRange(kFftWindowSamples, sample_rate_hz);
    if (!range.valid) {
        return 0.0f;
    }

    const std::size_t step = kFftWindowSamples - kFftOverlapSamples;
    const std::size_t segments = ((count - kFftWindowSamples) / step) + 1U;
    const std::size_t span = kFftWindowSamples + (segments - 1U) * step;
    const std::size_t base_start = count - span;

    std::array<float, kFftWindowSamples / 2U> local_psd{};

    for (std::size_t seg = 0U; seg < segments; ++seg) {
        const std::size_t seg_start = base_start + (seg * step);

        float mean = 0.0f;
#if CONFIG_MONITOR_FFT_DE_MEAN_ENABLE
        float sum = 0.0f;
        for (std::size_t i = 0U; i < kFftWindowSamples; ++i) {
            const std::size_t idx = (start_phys_idx + seg_start + i) % kStorageSamples;
            const float val = history[idx];
            fft_input_[2U * i] = val;
            sum += val;
        }
        mean = sum / static_cast<float>(kFftWindowSamples);
#endif

        for (std::size_t i = 0U; i < kFftWindowSamples; ++i) {
            float val = 0.0f;
#if CONFIG_MONITOR_FFT_DE_MEAN_ENABLE
            val = fft_input_[2U * i] - mean;
#else
            const std::size_t idx = (start_phys_idx + seg_start + i) % kStorageSamples;
            val = history[idx];
#endif

            const float window = 0.5f - 0.5f *
                std::cos(kTwoPi * static_cast<float>(i) / static_cast<float>(kFftWindowSamples - 1U));
            fft_input_[2U * i] = val * window;
            fft_input_[2U * i + 1U] = 0.0f;
        }

        if (dsps_fft2r_fc32(fft_input_.data(), static_cast<int>(kFftWindowSamples)) != ESP_OK) {
            return 0.0f;
        }
        if (dsps_bit_rev_fc32(fft_input_.data(), static_cast<int>(kFftWindowSamples)) != ESP_OK) {
            return 0.0f;
        }

        for (std::size_t bin = range.min_bin; bin <= range.max_bin; ++bin) {
            const float real = fft_input_[2U * bin];
            const float imag = fft_input_[2U * bin + 1U];
            const float power = (real * real) + (imag * imag);
            local_psd[bin] += power;
            psd_accum_[bin] += power;
        }
    }

    const float norm = 1.0f / static_cast<float>(segments);
    float max_val = 0.0f;
    std::size_t max_bin = 0U;

    for (std::size_t bin = range.min_bin; bin <= range.max_bin; ++bin) {
        local_psd[bin] *= norm;
        if (local_psd[bin] > max_val) {
            max_val = local_psd[bin];
            max_bin = bin;
        }
    }

    return (max_bin == 0U) ? 0.0f :
        ((static_cast<float>(max_bin) * sample_rate_hz) / static_cast<float>(kFftWindowSamples));
}

bool Monitor::ComputeSwayAndDamping(MonitorResult& result) noexcept {
    // 1. Sway statistics (entire disturbance buffer since entering DISTURBED)
    const std::size_t sway_count = BufferSize();
    if (sway_count < 3U) {
        result.roll_sway_pp_max = 0.0f;
        result.roll_sway_pp_mean = 0.0f;
        result.pitch_sway_pp_max = 0.0f;
        result.pitch_sway_pp_mean = 0.0f;
    } else {
        auto compute_sway_axis = [&](const std::array<float, kStorageSamples>& data,
                                     float& sway_pp_max,
                                     float& sway_pp_mean) {
            const float min_amp = config_.peak_min_amplitude_deg;
            const std::size_t min_spacing = config_.peak_min_spacing;

            std::size_t last_ext_idx = 0U;
            float last_ext_val = 0.0f;
            bool has_last_ext = false;
            float max_pp = 0.0f;
            float sum_pp = 0.0f;
            std::size_t pp_count = 0U;

            for (std::size_t i = 1U; i + 1U < sway_count; ++i) {
                const std::size_t idx_prev = PhysicalIndex(i - 1U);
                const std::size_t idx_curr = PhysicalIndex(i);
                const std::size_t idx_next = PhysicalIndex(i + 1U);

                const float prev = data[idx_prev];
                const float curr = data[idx_curr];
                const float next = data[idx_next];

                const bool is_peak = (curr > prev) && (curr > next) && (std::fabs(curr) >= min_amp);
                const bool is_trough = (curr < prev) && (curr < next) && (std::fabs(curr) >= min_amp);

                if (!(is_peak || is_trough)) {
                    continue;
                }

                if (has_last_ext && ((i - last_ext_idx) < min_spacing)) {
                    continue;
                }

                const float ext_val = curr;
                if (has_last_ext) {
                    const float pp = std::fabs(ext_val - last_ext_val);
                    if (pp > max_pp) {
                        max_pp = pp;
                    }
                    sum_pp += pp;
                    ++pp_count;
                }

                last_ext_idx = i;
                last_ext_val = ext_val;
                has_last_ext = true;
            }

            sway_pp_max = max_pp;
            sway_pp_mean = (pp_count > 0U) ? (sum_pp / static_cast<float>(pp_count)) : 0.0f;
        };

        compute_sway_axis(roll_history_, result.roll_sway_pp_max, result.roll_sway_pp_mean);
        compute_sway_axis(pitch_history_, result.pitch_sway_pp_max, result.pitch_sway_pp_mean);
    }

    return true;
}

void Monitor::CheckFailureEvents() noexcept {
    const auto events = imu_.get_motion_events();
    if (events.free_fall) {
        PublishFailure(FailureEvent::FreeFall);
    }

#if CONFIG_MONITOR_AE_MODE_GPIO
    std::uint32_t ae_events = 0U;
    portENTER_CRITICAL(&ae_mux_);
    ae_events = pending_ae_events_;
    pending_ae_events_ = 0U;
    portEXIT_CRITICAL(&ae_mux_);
    while (ae_events > 0U) {
        PublishFailure(FailureEvent::AcousticEmission);
        --ae_events;
    }
#else
    if (!adc_initialized_ || adc_handle_ == nullptr) {
        return;
    }

    int raw = 0;
    const auto handle = static_cast<adc_oneshot_unit_handle_t>(adc_handle_);
    if (adc_oneshot_read(handle,
                         static_cast<adc_channel_t>(config_.ae_adc_channel),
                         &raw) == ESP_OK) {
        if (raw >= config_.ae_adc_threshold) {
            PublishFailure(FailureEvent::AcousticEmission);
        }
    }
#endif
}

void Monitor::PublishFailure(FailureEvent event) noexcept {
    FailureResult result{};
    result.event = event;
    result.timestamp_us = static_cast<std::uint64_t>(esp_timer_get_time());
    const esp_err_t post_err = esp_event_post(MONITOR_EVENT_BASE,
                                              MONITOR_EVENT_FAILURE,
                                              &result,
                                              sizeof(result),
                                              0);
    if (post_err != ESP_OK) {
        ++dropped_failure_events_;
        ESP_LOGW(kTag, "Failure event post failed: %s", esp_err_to_name(post_err));
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

void IRAM_ATTR Monitor::AeGpioIsr(void* arg) noexcept {
    auto* self = static_cast<Monitor*>(arg);
    if (self != nullptr) {
        portENTER_CRITICAL_ISR(&self->ae_mux_);
        if (self->pending_ae_events_ < UINT32_MAX) {
            ++self->pending_ae_events_;
        }
        portEXIT_CRITICAL_ISR(&self->ae_mux_);
    }
}

float Monitor::ComputeGmagNaturalFrequency() noexcept {
    const std::size_t count = BufferSize();
    if (count < 3U) {
        return 0.0f;
    }
    return ComputeAxisNaturalFrequency(gmag_history_, StartIndex(), count);
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

void Monitor::SetCalibrationBiases(const calibration::CalibrationBias& biases) noexcept {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("calib", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        calibration::Calibration::WriteBiases(handle, biases);
        nvs_close(handle);
    }
    calib_bias_ = biases;
}

#if CONFIG_MONITOR_DEBUG_DUMP
void Monitor::DumpDebugToSD(const ModalAxisResult& roll_modal, const ModalAxisResult& pitch_modal,
                            float freq_roll_hz, float freq_pitch_hz,
                            float zeta_roll, float zeta_pitch,
                            std::int64_t modal_elapsed_us) noexcept {
    FILE* f = fopen(CONFIG_APP_SD_MOUNT_POINT "/dbg_dump.csv", "a");
    if (f == nullptr) {
        static bool warned = false;
        if (!warned) {
            ESP_LOGW(kTag, "DEBUG_DUMP: Failed to open /sdcard/dbg_dump.csv (SD not mounted?)");
            warned = true;
        }
        return;
    }

    std::uint64_t now_us = static_cast<std::uint64_t>(esp_timer_get_time());
    std::size_t buf_size = BufferSize();
    float rate_hz = static_cast<float>(CONFIG_MONITOR_IMU_RATE_HZ);

    std::fprintf(f, ">>>SNAPSHOT\n");
    std::fprintf(f, "META,%llu,%u,%.3f\n",
                 static_cast<unsigned long long>(now_us),
                 static_cast<unsigned>(buf_size),
                 static_cast<double>(rate_hz));
    std::fprintf(f, "MODAL_TIME_US,%lld\n", static_cast<long long>(modal_elapsed_us));

    std::fprintf(f, "DECAY,R,%u,%u\n",
                 static_cast<unsigned>(roll_modal.decay.start_index),
                 static_cast<unsigned>(roll_modal.decay.count));
    std::fprintf(f, "DECAY,P,%u,%u\n",
                 static_cast<unsigned>(pitch_modal.decay.start_index),
                 static_cast<unsigned>(pitch_modal.decay.count));

    std::fprintf(f, "RESULT,R,%.6f,%.6f\n",
                 static_cast<double>(freq_roll_hz),
                 static_cast<double>(zeta_roll));
    std::fprintf(f, "RESULT,P,%.6f,%.6f\n",
                 static_cast<double>(freq_pitch_hz),
                 static_cast<double>(zeta_pitch));

    auto write_peaks = [&f](char axis, const PeakList& peaks) {
        std::fprintf(f, "PEAKS,%c,%u", axis, static_cast<unsigned>(peaks.count));
        for (std::size_t i = 0U; i < peaks.count; ++i) {
            std::fprintf(f, ",%.6f,%.6f",
                         static_cast<double>(peaks.amplitudes[i]),
                         static_cast<double>(peaks.times[i]));
        }
        std::fprintf(f, "\n");
    };
    write_peaks('R', roll_modal.pair_envelope);
    write_peaks('P', pitch_modal.pair_envelope);

    auto write_modal = [&f](char axis, const ModalAxisResult& modal) {
        std::fprintf(f, "COLLAPSED,%c,%u\n",
                     axis,
                     static_cast<unsigned>(modal.collapsed_extrema.count));
        std::fprintf(f, "PAIRS,%c,%u", axis, static_cast<unsigned>(modal.centerline_pairs.count));
        for (std::size_t i = 0U; i < modal.centerline_pairs.count; ++i) {
            const CenterlinePair& pair = modal.centerline_pairs.pairs[i];
            std::fprintf(f, ",%u,%.6f,%.6f,%.6f",
                         static_cast<unsigned>(pair.center_logical_index),
                         static_cast<double>(pair.center_value),
                         static_cast<double>(pair.amplitude),
                         static_cast<double>(pair.time_s));
        }
        std::fprintf(f, "\n");
    };
    write_modal('R', roll_modal);
    write_modal('P', pitch_modal);

    auto write_raw = [this, &f](char axis, const std::array<float, kStorageSamples>& history) {
        std::fprintf(f, "RAW,%c", axis);
        for (std::size_t i = 0U; i < kStorageSamples; ++i) {
            std::size_t idx = PhysicalIndex(i);
            std::fprintf(f, ",%.6f", static_cast<double>(history[idx]));
        }
        std::fprintf(f, "\n");
    };
    write_raw('R', roll_history_);
    write_raw('P', pitch_history_);

    std::fprintf(f, "<<<END\n");
    std::fclose(f);

    ESP_LOGI(kTag, "DEBUG_DUMP: Snapshot written to /sdcard/dbg_dump.csv (%u samples)",
             static_cast<unsigned>(buf_size));
}
#endif

} // namespace monitor
