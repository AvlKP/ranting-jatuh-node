#include "monitor.hpp"
#include "monitor_internal.hpp"

#include <cmath>
#include <algorithm>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "dsps_fft2r.h"
#include "esp_timer.h"
#include "esp_event.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_continuous.h"
#include "esp_err.h"
#include "esp_attr.h"
#include "soc/soc_caps.h"
#include "nvs_init.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

namespace monitor {

ESP_EVENT_DEFINE_BASE(MONITOR_EVENT_BASE);

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
#elif CONFIG_MONITOR_AE_MODE_ADC
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
#elif CONFIG_MONITOR_AE_MODE_SPECTRAL_ADC
    if (!InitAeSpectralAdc()) {
        return false;
    }
#endif

    const std::size_t fft_init_size = std::max(kFftWindowSamples, config_.spectral_window_size);
    const esp_err_t err = dsps_fft2r_init_fc32(nullptr, static_cast<int>(fft_init_size));
    if (err != ESP_OK) {
        return false;
    }

    fft_initialized_ = true;

#if CONFIG_MONITOR_IMU_CALIBRATION
    {
        if (runtime::EnsureNvsInitialized() != ESP_OK) {
            ESP_LOGE(kTag, "NVS init before calib open failed");
            return false;
        }
        ESP_LOGI(kTag, "Opening NVS namespace calib for imu_bias read");
        esp_err_t nvs_err = nvs_open("calib", NVS_READONLY, &calib_nvs_handle_);
        if (nvs_err == ESP_OK) {
            nvs_err = calibration::Calibration::ReadBiases(calib_nvs_handle_, calib_bias_);
            ESP_LOGI(kTag, "Calibration bias read result: %s", esp_err_to_name(nvs_err));
            nvs_close(calib_nvs_handle_);
            calib_nvs_handle_ = 0;
        } else {
            ESP_LOGW(kTag, "Calibration NVS open failed: %s", esp_err_to_name(nvs_err));
        }
    }
#endif

    return true;
}

bool Monitor::Update(float dt_s) noexcept {
    sensor::lsm6ds3::Value gyro{};
    sensor::lsm6ds3::Value accel{};
    if (!ReadImu(gyro, accel)) {
        return false;
    }

#if CONFIG_MONITOR_IMU_MOUNT_LID
    const float calib_ax = -(accel.x - calib_bias_.ax);
    const float calib_ay = accel.y - calib_bias_.ay;
    const float calib_az = -(accel.z - calib_bias_.az);
    const float calib_gx = -(gyro.x - calib_bias_.gx);
    const float calib_gy = gyro.y - calib_bias_.gy;
    const float calib_gz = -(gyro.z - calib_bias_.gz);
#else
    const float calib_ax = accel.x - calib_bias_.ax;
    const float calib_ay = accel.y - calib_bias_.ay;
    const float calib_az = accel.z - calib_bias_.az;
    const float calib_gx = gyro.x - calib_bias_.gx;
    const float calib_gy = gyro.y - calib_bias_.gy;
    const float calib_gz = gyro.z - calib_bias_.gz;
#endif

    const std::array<float, 3> accel_vec{calib_ax, calib_ay, calib_az};
    const std::array<float, 3> gyro_vec{calib_gx, calib_gy, calib_gz};
    filter_.update(accel_vec, gyro_vec, dt_s);
    const float gmag = std::sqrt((calib_gx * calib_gx) + (calib_gy * calib_gy) + (calib_gz * calib_gz));
    float tkeo = 0.0f;
    static_cast<void>(tkeo_window_.Push(gmag, tkeo));

    float current_roll = filter_.roll();
    float current_pitch = filter_.pitch();



    PushSample(current_roll, current_pitch,
               calib_gx, calib_gy, calib_gz,
               calib_ax, calib_ay, calib_az,
               gmag, tkeo);

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
                        ESP_LOGI(kTag, "IDLE baseline updated.");
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

void Monitor::CheckFailureEvents() noexcept {
    const auto events = imu_.get_motion_events();
    if (events.free_fall) {
        const auto now_us = static_cast<std::uint64_t>(esp_timer_get_time());
        const auto cooldown_us = static_cast<std::uint64_t>(config_.freefall_debounce_ms) * 1000ULL;
        if (last_freefall_publish_us_ == 0U || (now_us - last_freefall_publish_us_) >= cooldown_us) {
            PublishFailure(FailureEvent::FreeFall);
            last_freefall_publish_us_ = now_us;
        }
    }

    CheckAeFailureEvents();
}

void Monitor::SetCalibrationBiases(const calibration::CalibrationBias& biases) noexcept {
    if (runtime::EnsureNvsInitialized() != ESP_OK) {
        ESP_LOGE(kTag, "NVS init before calibration write failed");
        calib_bias_ = biases;
        return;
    }
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("calib", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = calibration::Calibration::WriteBiases(handle, biases);
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    ESP_LOGI(kTag, "Calibration bias write result: %s", esp_err_to_name(err));
    calib_bias_ = biases;
}

std::uint32_t Monitor::PendingAeEvents() const noexcept {
    std::uint32_t events = 0U;
    portENTER_CRITICAL(&ae_mux_);
    events = pending_ae_events_;
    portEXIT_CRITICAL(&ae_mux_);
    return events;
}

} // namespace monitor
