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

    ae_gpio_event_ = false;
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
        static_cast<TaskHandle_t*>(nullptr),
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

    const std::array<float, 3> accel_vec{accel.x, accel.y, accel.z};
    const std::array<float, 3> gyro_vec{gyro.x, gyro.y, gyro.z};
    filter_.update(accel_vec, gyro_vec, dt_s);

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

    PushSample(current_roll, current_pitch, accel.x, accel.y, accel.z);

    ESP_LOGD(kTag, "Raw stream: ax=%.3f ay=%.3f az=%.3f gx=%.3f gy=%.3f gz=%.3f r=%.3f p=%.3f",
             accel.x, accel.y, accel.z, gyro.x, gyro.y, gyro.z, current_roll, current_pitch);

    StreamSample sample{};
    sample.accel_x = accel.x;
    sample.accel_y = accel.y;
    sample.accel_z = accel.z;
    sample.gyro_x = gyro.x;
    sample.gyro_y = gyro.y;
    sample.gyro_z = gyro.z;
    sample.roll = current_roll;
    sample.pitch = current_pitch;
    sample.timestamp_us = static_cast<std::uint64_t>(esp_timer_get_time());
    sample.state = state_;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        stream_samples_[stream_write_index_] = sample;
        stream_write_index_ = (stream_write_index_ + 1U) % kMaxStreamSamples;
        if (stream_count_ < kMaxStreamSamples) {
            ++stream_count_;
        }
    }

#ifdef CONFIG_APP_DEBUG_CSV_LOGS
    {
        const std::size_t idx = debug_write_index_.load(std::memory_order_relaxed);
        debug_samples_[idx] = sample;
        debug_write_index_.store((idx + 1U) % kDebugRingSize, std::memory_order_release);
        std::size_t prev_count = debug_count_.load(std::memory_order_acquire);
        if (prev_count < kDebugRingSize) {
            debug_count_.store(prev_count + 1U, std::memory_order_release);
        }
    }
#endif

    CheckFailureEvents();

    if ((sample_count_ >= kStorageSamples) && (write_index_ == 0U)) {
        if (state_ == NodeState::IDLE) {
            MonitorResult result{};
            if (ComputeStats(result)) {
                idle_5min_roll_var_ = result.roll_variance;
                idle_5min_pitch_var_ = result.pitch_variance;
                has_baseline_variance_ = true;

                // Calculate baseline accel error variance
                if (baseline_sample_count_ > 1U) {
                    const float n = static_cast<float>(baseline_sample_count_);
                    accel_err_baseline_var_ = (baseline_accum_sq_sum_ - (baseline_accum_sum_ * baseline_accum_sum_) / n) / (n - 1.0f);
                    if (accel_err_baseline_var_ < 0.0f) accel_err_baseline_var_ = 0.0f;
                    has_accel_err_baseline_ = true;
                }
                baseline_accum_sum_ = 0.0f;
                baseline_accum_sq_sum_ = 0.0f;
                baseline_sample_count_ = 0U;
                
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
                
                esp_event_post(MONITOR_EVENT_BASE,
                               MONITOR_EVENT_RESULT,
                               &result,
                               sizeof(result),
                               0);
                
                ESP_LOGI(kTag, "IDLE baseline updated. roll_var=%.3f pitch_var=%.3f accel_err_baseline_var=%.6f",
                         idle_5min_roll_var_, idle_5min_pitch_var_, accel_err_baseline_var_);
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

void Monitor::PushSample(float roll, float pitch, float ax, float ay, float az) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);

    // 1. Maintain roll/pitch short buffer and rolling sums
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
    roll_short_sum_ += roll;
    roll_short_sq_sum_ += (roll * roll);
    pitch_short_sum_ += pitch;
    pitch_short_sq_sum_ += (pitch * pitch);

    short_write_index_ = (short_write_index_ + 1U) % kShortBufferSamples;

    // 2. Compute raw accel error and update its short buffer
    const float accel_err = std::abs(std::sqrt(ax * ax + ay * ay + az * az) - 1.0f);

    if (accel_err_short_sample_count_ == kAccelErrShortBufferSamples) {
        const float old_val = accel_err_short_[accel_err_short_write_index_];
        accel_err_short_sum_ -= old_val;
        accel_err_short_sq_sum_ -= (old_val * old_val);
    } else {
        ++accel_err_short_sample_count_;
    }

    accel_err_short_[accel_err_short_write_index_] = accel_err;
    accel_err_short_sum_ += accel_err;
    accel_err_short_sq_sum_ += (accel_err * accel_err);
    accel_err_short_write_index_ = (accel_err_short_write_index_ + 1U) % kAccelErrShortBufferSamples;

    // Compute live O(1) variance of accel error
    float accel_err_var = 0.0f;
    if (accel_err_short_sample_count_ > 1U) {
        const float n = static_cast<float>(accel_err_short_sample_count_);
        accel_err_var = (accel_err_short_sq_sum_ - (accel_err_short_sum_ * accel_err_short_sum_) / n) / (n - 1.0f);
        if (accel_err_var < 0.0f) accel_err_var = 0.0f;
    }

    // Accumulate baseline sums during IDLE
    if (state_ == NodeState::IDLE) {
        baseline_accum_sum_ += accel_err;
        baseline_accum_sq_sum_ += (accel_err * accel_err);
        baseline_sample_count_++;
    }

    // 3. FSM State Transitions
    const float abs_min_var = static_cast<float>(CONFIG_MONITOR_ABS_MIN_ACCEL_VAR_X1000000) / 1000000.0f;
    const float k_high = static_cast<float>(CONFIG_MONITOR_K_HIGH_X100) / 100.0f;
    const float k_low = static_cast<float>(CONFIG_MONITOR_K_LOW_X100) / 100.0f;

    const float threshold_high = std::max(accel_err_baseline_var_ * k_high, abs_min_var);
    const float threshold_low = std::max(accel_err_baseline_var_ * k_low, abs_min_var);

    if (state_ == NodeState::IDLE) {
        roll_history_[write_index_] = roll;
        pitch_history_[write_index_] = pitch;
        write_index_ = (write_index_ + 1U) % kStorageSamples;
        if (sample_count_ < kStorageSamples) {
            ++sample_count_;
        }

        if (has_accel_err_baseline_ && accel_err_short_sample_count_ == kAccelErrShortBufferSamples) {
            if (accel_err_var > threshold_high) {
                state_ = NodeState::DISTURBED;
                
                // Copy short buffer roll/pitch to start of history as pre-roll
                write_index_ = 0U;
                sample_count_ = 0U;
                
                const std::size_t count = short_sample_count_;
                for (std::size_t i = 0U; i < count; ++i) {
                    const std::size_t idx = (short_write_index_ + kShortBufferSamples - count + i) % kShortBufferSamples;
                    roll_history_[i] = roll_short_[idx];
                    pitch_history_[i] = pitch_short_[idx];
                }
                write_index_ = count;
                sample_count_ = count;
                
                disturbed_exit_debounce_counter_ = 0U;
                
                ESP_LOGI(kTag, "Transition: IDLE -> DISTURBED (accel_err_var=%.6f > th=%.6f)",
                         accel_err_var, threshold_high);
            }
        }
    } else if (state_ == NodeState::DISTURBED) {
        roll_history_[write_index_] = roll;
        pitch_history_[write_index_] = pitch;
        write_index_ = (write_index_ + 1U) % kStorageSamples;
        if (sample_count_ < kStorageSamples) {
            ++sample_count_;
        }

        bool transitioned = false;
        if (accel_err_var < threshold_low) {
            ++disturbed_exit_debounce_counter_;
            if (disturbed_exit_debounce_counter_ >= static_cast<std::size_t>(CONFIG_MONITOR_DISTURBED_EXIT_DEBOUNCE)) {
                state_ = NodeState::IDLE;
                transitioned = true;
                
                ESP_LOGI(kTag, "Transition: DISTURBED -> IDLE (accel_err_var=%.6f < th=%.6f)",
                         accel_err_var, threshold_low);
                
                static_cast<void>(ComputeAndPublish(NodeState::DISTURBED, true));
            }
        } else {
            disturbed_exit_debounce_counter_ = 0U;
        }

        if (!transitioned && sample_count_ >= (kStorageSamples - static_cast<std::size_t>(CONFIG_MONITOR_N_DPAD))) {
            static_cast<void>(ComputeAndPublish(NodeState::DISTURBED, false));
            ESP_LOGI(kTag, "DISTURBED Buffer Refreshed");
            
            write_index_ = 0U;
            sample_count_ = 0U;
            
            const std::size_t count = short_sample_count_;
            for (std::size_t i = 0U; i < count; ++i) {
                const std::size_t idx = (short_write_index_ + kShortBufferSamples - count + i) % kShortBufferSamples;
                roll_history_[i] = roll_short_[idx];
                pitch_history_[i] = pitch_short_[idx];
            }
            write_index_ = count;
            sample_count_ = count;
        }
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
            PeakList roll_peaks, pitch_peaks;
            DecayRegion roll_decay = FindDecayRegion(roll_history_, roll_peaks);
            DecayRegion pitch_decay = FindDecayRegion(pitch_history_, pitch_peaks);
            
            result.natural_freq_roll_hz = ComputeAxisNaturalFrequency(roll_history_, roll_decay.start_index, roll_decay.count);
            result.natural_freq_pitch_hz = ComputeAxisNaturalFrequency(pitch_history_, pitch_decay.start_index, pitch_decay.count);
            result.natural_freq_hz = std::max(result.natural_freq_roll_hz, result.natural_freq_pitch_hz);
            
            result.roll_damping_ratio = ComputeDampingRegression(roll_peaks, result.natural_freq_roll_hz);
            result.pitch_damping_ratio = ComputeDampingRegression(pitch_peaks, result.natural_freq_pitch_hz);
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

    esp_event_post(MONITOR_EVENT_BASE,
                   MONITOR_EVENT_RESULT,
                   &result,
                   sizeof(result),
                   0);

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

float Monitor::ComputeAxisNaturalFrequency(const std::array<float, kStorageSamples>& history, std::size_t start_phys_idx, std::size_t count) noexcept {
    if (count == 0U) {
        return 0.0f;
    }

    const float sample_rate_hz = static_cast<float>(CONFIG_MONITOR_IMU_RATE_HZ);

    if (count < kFftWindowSamples) {
        const std::size_t fft_size = (count < 512U) ? 512U : 1024U;

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

        for (std::size_t bin = 1U; bin < (fft_size / 2U); ++bin) {
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

        return (static_cast<float>(max_bin) * sample_rate_hz) / static_cast<float>(fft_size);
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

        for (std::size_t bin = 1U; bin < (kFftWindowSamples / 2U); ++bin) {
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

    for (std::size_t bin = 1U; bin < (kFftWindowSamples / 2U); ++bin) {
        local_psd[bin] *= norm;
        if (local_psd[bin] > max_val) {
            max_val = local_psd[bin];
            max_bin = bin;
        }
    }

    return (static_cast<float>(max_bin) * sample_rate_hz) / static_cast<float>(kFftWindowSamples);
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
    if (ae_gpio_event_) {
        ae_gpio_event_ = false;
        PublishFailure(FailureEvent::AcousticEmission);
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
    esp_event_post(MONITOR_EVENT_BASE,
                   MONITOR_EVENT_FAILURE,
                   &result,
                   sizeof(result),
                   0);
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
        self->ae_gpio_event_ = true;
    }
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

#ifdef CONFIG_APP_DEBUG_CSV_LOGS
void Monitor::GetDebugSamples(StreamSample* out_samples, std::size_t& out_len, std::size_t max_len) noexcept {
    const std::size_t count = debug_count_.load(std::memory_order_acquire);
    out_len = (count < max_len) ? count : max_len;
    if (out_len == 0U || out_samples == nullptr) {
        return;
    }

    const std::size_t wr_idx = debug_write_index_.load(std::memory_order_acquire);
    std::size_t start_idx = (count < kDebugRingSize) ? 0U : wr_idx;
    for (std::size_t i = 0U; i < out_len; ++i) {
        out_samples[i] = debug_samples_[(start_idx + i) % kDebugRingSize];
    }

    const std::size_t remaining = count - out_len;
    debug_count_.store(remaining, std::memory_order_release);
    if (remaining == 0U) {
        debug_write_index_.store(0U, std::memory_order_release);
    }
}
#endif

} // namespace monitor
