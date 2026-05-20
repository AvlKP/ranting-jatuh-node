#include "monitor.hpp"

#include <cmath>
#include <cstdio>

#include "dsps_fft2r.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_attr.h"

namespace monitor {

namespace {

constexpr float kPi = 3.14159265358979323846f;
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

    if (!imu_.configure_motion_detection(0x09, 0x02, static_cast<std::uint8_t>(CONFIG_MONITOR_FREEFALL_THS))) {
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
    return true;
}

void Monitor::RegisterCallback(EventCb cb, void* ctx) noexcept {
    callback_ = cb;
    callback_ctx_ = ctx;
}

void Monitor::RegisterFailureCallback(FailureCb cb, void* ctx) noexcept {
    failure_callback_ = cb;
    failure_callback_ctx_ = ctx;
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

    CheckFailureEvents();

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

    if (!ComputeSwayAndDamping(result)) {
        return false;
    }

    if (config_.debug_enabled) {
        std::printf("monitor_debug: roll_mean=%.3f roll_var=%.3f pitch_mean=%.3f pitch_var=%.3f "
                    "roll_pp_max=%.3f roll_pp_mean=%.3f pitch_pp_max=%.3f pitch_pp_mean=%.3f "
                    "roll_zeta=%.4f pitch_zeta=%.4f freq=%.3fHz samples=%u ts_us=%llu\n",
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
                    static_cast<unsigned>(result.sample_count),
                    static_cast<unsigned long long>(result.timestamp_us));
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

bool Monitor::ComputeSwayAndDamping(MonitorResult& result) noexcept {
    const std::size_t count = BufferSize();
    if (count < 3U) {
        result.roll_sway_pp_max = 0.0f;
        result.roll_sway_pp_mean = 0.0f;
        result.pitch_sway_pp_max = 0.0f;
        result.pitch_sway_pp_mean = 0.0f;
        result.roll_damping_ratio = 0.0f;
        result.pitch_damping_ratio = 0.0f;
        return true;
    }

    // TODO: Add wind gust/storm state; sway amplitude should be computed only during that state.
    // TODO: Damping ratio ignores boundary conditions when wind extends beyond window; fix after gust/storm state.

    auto compute_axis = [&](const std::array<float, kStorageSamples>& data,
                            float& sway_pp_max,
                            float& sway_pp_mean,
                            float& damping_ratio) -> bool {
        const float min_amp = config_.peak_min_amplitude_deg;
        const std::size_t min_spacing = config_.peak_min_spacing;

        std::size_t last_ext_idx = 0U;
        float last_ext_val = 0.0f;
        bool has_last_ext = false;
        float max_pp = 0.0f;
        float sum_pp = 0.0f;
        std::size_t pp_count = 0U;

        float max_abs = 0.0f;
        std::size_t max_idx = 0U;
        bool has_max = false;

        for (std::size_t i = 1U; i + 1U < count; ++i) {
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

            const float abs_val = std::fabs(ext_val);
            if (!has_max || abs_val > max_abs) {
                max_abs = abs_val;
                max_idx = i;
                has_max = true;
            }

            last_ext_idx = i;
            last_ext_val = ext_val;
            has_last_ext = true;
        }

        sway_pp_max = max_pp;
        sway_pp_mean = (pp_count > 0U) ? (sum_pp / static_cast<float>(pp_count)) : 0.0f;

        if (!has_max) {
            damping_ratio = 0.0f;
            return true;
        }

        constexpr std::size_t kDecaySpan = 3U;
        std::size_t found = 0U;
        float x1 = 0.0f;
        float x2 = 0.0f;
        std::size_t last_idx = 0U;
        bool has_last = false;

        for (std::size_t i = 1U; i + 1U < count; ++i) {
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

            if (has_last && ((i - last_idx) < min_spacing)) {
                continue;
            }

            last_idx = i;
            has_last = true;

            if (i < max_idx) {
                continue;
            }

            if (found == 0U && i == max_idx) {
                x1 = std::fabs(curr);
                found = 1U;
                continue;
            }

            if (found > 0U && found <= kDecaySpan) {
                ++found;
                if (found == (kDecaySpan + 1U)) {
                    x2 = std::fabs(curr);
                    break;
                }
            }
        }

        if (found == (kDecaySpan + 1U) && x2 > 0.0f && x1 > x2) {
            const float delta = std::log(x1 / x2) / static_cast<float>(kDecaySpan);
            damping_ratio = delta / std::sqrt((4.0f * kPi * kPi) + (delta * delta));
        } else {
            damping_ratio = 0.0f;
        }
        return true;
    };

    if (!compute_axis(roll_history_, result.roll_sway_pp_max, result.roll_sway_pp_mean, result.roll_damping_ratio)) {
        return false;
    }
    if (!compute_axis(pitch_history_, result.pitch_sway_pp_max, result.pitch_sway_pp_mean, result.pitch_damping_ratio)) {
        return false;
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
    if (failure_callback_ == nullptr) {
        return;
    }

    FailureResult result{};
    result.event = event;
    result.timestamp_us = static_cast<std::uint64_t>(esp_timer_get_time());
    failure_callback_(failure_callback_ctx_, result);
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

} // namespace monitor
