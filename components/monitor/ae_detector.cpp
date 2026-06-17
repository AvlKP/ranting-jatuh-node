/// @file ae_detector.cpp
/// @brief Acoustic-emission sensor handling for the monitor component.
/// @details Owns GPIO ISR draining, one-shot ADC threshold reading, and the
/// spectral ADC pipeline (windowing, FFT, energy/gradient detection). The
/// spectral branch runs on its own FreeRTOS task; the GPIO/ADC branches are
/// sampled by Monitor::CheckFailureEvents in monitor.cpp.
/// @ingroup monitor

#include "monitor.hpp"
#include "monitor_internal.hpp"

#include <cmath>
#include <algorithm>

#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_continuous.h"
#include "dsps_fft2r.h"
#include "dsps_view.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "soc/soc_caps.h"

namespace monitor {

namespace {

#if CONFIG_IDF_TARGET_ESP32S3
constexpr adc_digi_output_format_t kAeAdcOutputFormat = ADC_DIGI_OUTPUT_FORMAT_TYPE2;
#else
constexpr adc_digi_output_format_t kAeAdcOutputFormat = ADC_DIGI_OUTPUT_FORMAT_TYPE1;
#endif

} // namespace

bool AeSpectralDetector::ValidateConfig(const MonitorConfig& config) noexcept {
    const std::size_t window = config.spectral_window_size;
    if ((window < 4U) || (window > kAeSpectralMaxWindowSamples) || !IsPowerOfTwo(window)) {
        return false;
    }
    if ((config.spectral_sample_rate_hz == 0U) ||
        (config.spectral_bin_start == 0U) ||
        (config.spectral_bin_start > config.spectral_bin_end) ||
        (config.spectral_bin_end >= (window / 2U))) {
        return false;
    }
    if ((config.spectral_gradient_window < 2U) ||
        (config.spectral_gradient_window > kAeSpectralMaxGradientSamples)) {
        return false;
    }
    if ((config.spectral_leak_alpha < 0.0f) || (config.spectral_leak_alpha > 1.0f) ||
        (config.spectral_ewma_alpha <= 0.0f) || (config.spectral_ewma_alpha > 1.0f) ||
        (config.spectral_danger_multiplier <= 0.0f)) {
        return false;
    }
    return true;
}

float AeSpectralDetector::ComputeEnergyFromComplexBins(const float* fft_data,
                                                       std::size_t fft_size,
                                                       std::size_t bin_start,
                                                       std::size_t bin_end) noexcept {
    if ((fft_data == nullptr) || (fft_size < 4U) ||
        (bin_start == 0U) || (bin_start > bin_end) || (bin_end >= (fft_size / 2U))) {
        return 0.0f;
    }

    float sum = 0.0f;
    for (std::size_t bin = bin_start; bin <= bin_end; ++bin) {
        const float real = fft_data[2U * bin];
        const float imag = fft_data[(2U * bin) + 1U];
        sum += std::sqrt((real * real) + (imag * imag));
    }
    return sum / 1000.0f;
}

bool AeSpectralDetector::ComputeWindowEnergy(const std::uint16_t* samples,
                                             const MonitorConfig& config,
                                             float* fft_buffer,
                                             std::size_t fft_buffer_len,
                                             float& out_energy) noexcept {
    out_energy = 0.0f;
    if ((samples == nullptr) || (fft_buffer == nullptr) ||
        !ValidateConfig(config) ||
        (fft_buffer_len < (config.spectral_window_size * 2U))) {
        return false;
    }

    const std::size_t window_size = config.spectral_window_size;

    std::fill(fft_buffer, fft_buffer + (window_size * 2U), 0.0f);
    for (std::size_t i = 0U; i < window_size; ++i) {
        const float denom = static_cast<float>(window_size - 1U);
        const float window = 0.54f - (0.46f * std::cos(kTwoPi * static_cast<float>(i) / denom));
        fft_buffer[2U * i] = static_cast<float>(samples[i]) * window;
        fft_buffer[(2U * i) + 1U] = 0.0f;
    }

    if (dsps_fft2r_fc32(fft_buffer, static_cast<int>(window_size)) != ESP_OK) {
        return false;
    }
    if (dsps_bit_rev_fc32(fft_buffer, static_cast<int>(window_size)) != ESP_OK) {
        return false;
    }

    out_energy = ComputeEnergyFromComplexBins(fft_buffer,
                                              window_size,
                                              config.spectral_bin_start,
                                              config.spectral_bin_end);
    return true;
}

void AeSpectralDetector::Reset() noexcept {
    gradient_ring_ = {};
    gradient_write_index_ = 0U;
    gradient_count_ = 0U;
    previous_energy_ = 0.0f;
    has_previous_energy_ = false;
    integrator_ = 0.0f;
    ewma_mean_ = 0.0f;
    ewma_variance_ = 100.0f;
    sigma_ = 10.0f;
    latch_until_ms_ = 0U;
    last_publish_ms_ = 0U;
    latch_active_ = false;
    danger_active_ = false;
}

AeSpectralUpdateResult AeSpectralDetector::UpdateEnergy(float energy,
                                                        std::uint64_t now_ms,
                                                        const MonitorConfig& config) noexcept {
    AeSpectralUpdateResult result{};
    result.energy = energy;

    const bool was_latch_active = latch_active_;
    const bool was_danger_active = danger_active_;

    if (latch_active_ && (now_ms >= latch_until_ms_)) {
        latch_active_ = false;
    }

    if (has_previous_energy_ && ((energy - previous_energy_) > config.spectral_jump_threshold)) {
        latch_active_ = true;
        latch_until_ms_ = now_ms + static_cast<std::uint64_t>(config.spectral_latch_duration_ms);
    }
    previous_energy_ = energy;
    has_previous_energy_ = true;

    integrator_ = (config.spectral_leak_alpha * integrator_) + energy;
    gradient_ring_[gradient_write_index_] = integrator_;

    const bool gradient_full = gradient_count_ >= config.spectral_gradient_window;
    if (!gradient_full) {
        ++gradient_count_;
    }

    const std::size_t oldest_index = gradient_full ? ((gradient_write_index_ + 1U) % config.spectral_gradient_window) : 0U;
    float gradient = integrator_ - gradient_ring_[oldest_index];

    if (gradient < 0.0f) {
        gradient = 0.0f;
    }

    sigma_ = std::max(0.1f, std::sqrt(std::max(0.0f, ewma_variance_)));
    const float z_score = std::fabs((gradient - ewma_mean_) / sigma_);

    if (z_score <= 3.0f) {
        ewma_mean_ = (config.spectral_ewma_alpha * gradient) + ((1.0f - config.spectral_ewma_alpha) * ewma_mean_);
        const float diff_new = gradient - ewma_mean_;
        ewma_variance_ = (config.spectral_ewma_alpha * diff_new * diff_new) +
            ((1.0f - config.spectral_ewma_alpha) * ewma_variance_);
        sigma_ = std::max(0.1f, std::sqrt(std::max(0.0f, ewma_variance_)));
    }

    const float threshold = ewma_mean_ + (config.spectral_danger_multiplier * sigma_);
    danger_active_ = gradient_full && (gradient > threshold);

    gradient_write_index_ = (gradient_write_index_ + 1U) % config.spectral_gradient_window;

    const bool time_since_last_publish_ok = (last_publish_ms_ == 0U) ||
        (config.spectral_min_publish_interval_ms == 0U) ||
        ((now_ms - last_publish_ms_) >= static_cast<std::uint64_t>(config.spectral_min_publish_interval_ms));

    result.gradient = gradient;
    result.danger_threshold = threshold;
    result.ewma_mean = ewma_mean_;
    result.sigma = sigma_;
    result.latch_active = latch_active_;
    result.latch_started = !was_latch_active && latch_active_;
    result.danger_active = danger_active_;
    result.danger_started = !was_danger_active && danger_active_;
    result.should_publish = danger_active_ && latch_active_ && time_since_last_publish_ok;
    if (result.should_publish) {
        last_publish_ms_ = now_ms;
    }
    return result;
}

bool Monitor::InitAeSpectralAdc() noexcept {
#if CONFIG_MONITOR_AE_MODE_SPECTRAL_ADC
    if ((config_.ae_adc_channel < 0) || !AeSpectralDetector::ValidateConfig(config_)) {
        ESP_LOGE(kTag,
                 "Invalid AE spectral config channel=%ld rate=%lu window=%u bins=%u..%u grad=%u",
                 static_cast<long>(config_.ae_adc_channel),
                 static_cast<unsigned long>(config_.spectral_sample_rate_hz),
                 static_cast<unsigned>(config_.spectral_window_size),
                 static_cast<unsigned>(config_.spectral_bin_start),
                 static_cast<unsigned>(config_.spectral_bin_end),
                 static_cast<unsigned>(config_.spectral_gradient_window));
        return false;
    }

    adc_continuous_handle_cfg_t handle_cfg{};
    handle_cfg.max_store_buf_size = 1024U;
    handle_cfg.conv_frame_size = static_cast<std::uint32_t>(ae_adc_read_buffer_.size());

    adc_continuous_handle_t handle = nullptr;
    esp_err_t err = adc_continuous_new_handle(&handle_cfg, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "AE spectral ADC handle init failed: %s", esp_err_to_name(err));
        return false;
    }

    adc_digi_pattern_config_t pattern{};
    pattern.atten = ADC_ATTEN_DB_11;
    pattern.channel = static_cast<std::uint8_t>(config_.ae_adc_channel);
    pattern.unit = ADC_UNIT_1;
    pattern.bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;

    adc_continuous_config_t dig_cfg{};
    dig_cfg.pattern_num = 1U;
    dig_cfg.adc_pattern = &pattern;
    dig_cfg.sample_freq_hz = config_.spectral_sample_rate_hz;
    dig_cfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;
    dig_cfg.format = kAeAdcOutputFormat;

    err = adc_continuous_config(handle, &dig_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "AE spectral ADC config failed: %s", esp_err_to_name(err));
        adc_continuous_deinit(handle);
        return false;
    }

    err = adc_continuous_start(handle);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "AE spectral ADC start failed: %s", esp_err_to_name(err));
        adc_continuous_deinit(handle);
        return false;
    }

    ae_spectral_detector_.Reset();
    adc_handle_ = handle;
    adc_initialized_ = true;
    ESP_LOGI(kTag, "AE spectral ADC started channel=%ld rate=%lu window=%u bins=%u..%u",
             static_cast<long>(config_.ae_adc_channel),
             static_cast<unsigned long>(config_.spectral_sample_rate_hz),
             static_cast<unsigned>(config_.spectral_window_size),
             static_cast<unsigned>(config_.spectral_bin_start),
             static_cast<unsigned>(config_.spectral_bin_end));
    return true;
#else
    return false;
#endif
}

void Monitor::ProcessAeSpectralWindow() noexcept {
#if CONFIG_MONITOR_AE_MODE_SPECTRAL_ADC
    float energy = 0.0f;
    if (!AeSpectralDetector::ComputeWindowEnergy(ae_sample_window_.data(),
                                                 config_,
                                                 ae_fft_buffer_.data(),
                                                 ae_fft_buffer_.size(),
                                                 energy)) {
        ESP_LOGW(kTag, "AE spectral window processing failed");
        return;
    }

    const std::uint64_t now_ms = static_cast<std::uint64_t>(esp_timer_get_time()) / 1000ULL;
    const AeSpectralUpdateResult update = ae_spectral_detector_.UpdateEnergy(energy, now_ms, config_);
    if (update.should_publish) {
        PublishFailure(FailureEvent::AcousticEmission);
        ESP_LOGI(kTag,
                 "AE spectral detection energy=%.3f gradient=%.3f threshold=%.3f latch=%d danger=%d",
                 static_cast<double>(update.energy),
                 static_cast<double>(update.gradient),
                 static_cast<double>(update.danger_threshold),
                 update.latch_active,
                 update.danger_active);
    }

    ++ae_spectral_windows_;
    if ((ae_spectral_windows_ & 0x3FFU) == 0U) {
        const UBaseType_t words = uxTaskGetStackHighWaterMark(nullptr);
        ESP_LOGI(kTag, "ae_spectral_task stack_high_water=%lu bytes windows=%lu",
                 static_cast<unsigned long>(words * sizeof(StackType_t)),
                 static_cast<unsigned long>(ae_spectral_windows_));
    }
#endif
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

void Monitor::CheckAeFailureEvents() noexcept {
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
#elif CONFIG_MONITOR_AE_MODE_ADC
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
#elif CONFIG_MONITOR_AE_MODE_SPECTRAL_ADC
    // AE spectral task owns ADC sampling and acoustic-emission publication.
#else
    (void)0;
#endif
}

} // namespace monitor
