/// @file monitor_task.cpp
/// @brief FreeRTOS task orchestration for the monitor component.
/// @details Owns creation of the monitor sampling task and the optional AE
/// spectral ADC task, plus their fixed-rate loops. No signal processing lives
/// here; task bodies delegate to Monitor::Update and Monitor::AeSpectralTaskLoop.
/// @ingroup monitor

#include "monitor.hpp"
#include "monitor_internal.hpp"

#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_continuous.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "soc/soc_caps.h"

namespace monitor {

namespace {

constexpr std::size_t kTaskStackSize = 6144U;
static_assert(kTaskStackSize >= 2048U, "Monitor task stack too small for ESP-IDF task minimum.");
static_assert(kTaskStackSize <= 16384U, "Monitor task stack exceeds bounded RAM budget.");
constexpr std::size_t kAeSpectralTaskStackSize = 4096U;
static_assert(kAeSpectralTaskStackSize >= 2048U, "AE spectral task stack too small.");
static_assert(kAeSpectralTaskStackSize <= 8192U, "AE spectral task stack exceeds bounded RAM budget.");
constexpr UBaseType_t kTaskPriority = 5U;
constexpr UBaseType_t kAeSpectralTaskPriority = 4U;
constexpr BaseType_t kTaskCore = 1;
constexpr std::uint32_t kAeAdcReadTimeoutMs = 100U;

void MonitorTaskEntry(void* arg) noexcept {
    auto* self = static_cast<Monitor*>(arg);
    self->TaskLoop();
}

#if CONFIG_MONITOR_AE_MODE_SPECTRAL_ADC
void AeSpectralTaskEntry(void* arg) noexcept {
    auto* self = static_cast<Monitor*>(arg);
    self->AeSpectralTaskLoop();
}
#endif

} // namespace

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
        const std::uint32_t free_internal = static_cast<std::uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        const std::uint32_t largest_block = static_cast<std::uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        ESP_LOGE(kTag, "Failed to create monitor_task (stack=%u free_internal=%lu largest_block=%lu)",
                 static_cast<unsigned>(kTaskStackSize),
                 static_cast<unsigned long>(free_internal),
                 static_cast<unsigned long>(largest_block));
        return false;
    }
    ESP_LOGI(kTag, "monitor_task started on core %d, priority %u", kTaskCore, kTaskPriority);

#if CONFIG_MONITOR_AE_MODE_SPECTRAL_ADC
    const BaseType_t ae_ret = xTaskCreatePinnedToCore(
        AeSpectralTaskEntry,
        "ae_spectral_task",
        kAeSpectralTaskStackSize,
        this,
        kAeSpectralTaskPriority,
        &ae_spectral_task_handle_,
        kTaskCore);
    if (ae_ret != pdPASS) {
        const std::uint32_t free_internal = static_cast<std::uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        const std::uint32_t largest_block = static_cast<std::uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        ESP_LOGE(kTag, "Failed to create ae_spectral_task (stack=%u free_internal=%lu largest_block=%lu)",
                 static_cast<unsigned>(kAeSpectralTaskStackSize),
                 static_cast<unsigned long>(free_internal),
                 static_cast<unsigned long>(largest_block));
        return false;
    }
    ESP_LOGI(kTag, "ae_spectral_task started on core %d, priority %u", kTaskCore, kAeSpectralTaskPriority);
#endif
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

void Monitor::AeSpectralTaskLoop() noexcept {
#if CONFIG_MONITOR_AE_MODE_SPECTRAL_ADC
    auto* handle = static_cast<adc_continuous_handle_t>(adc_handle_);
    if (!adc_initialized_ || handle == nullptr) {
        ESP_LOGE(kTag, "ae_spectral_task missing ADC handle");
        vTaskDelete(nullptr);
        return;
    }

    while (true) {
        std::uint32_t bytes_read = 0U;
        const esp_err_t read_err = adc_continuous_read(handle,
                                                       ae_adc_read_buffer_.data(),
                                                       static_cast<std::uint32_t>(ae_adc_read_buffer_.size()),
                                                       &bytes_read,
                                                       kAeAdcReadTimeoutMs);
        if (read_err == ESP_ERR_TIMEOUT) {
            continue;
        }
        if (read_err != ESP_OK) {
            ESP_LOGW(kTag, "AE spectral ADC read failed: %s", esp_err_to_name(read_err));
            vTaskDelay(pdMS_TO_TICKS(10U));
            continue;
        }

        for (std::uint32_t offset = 0U;
             (offset + SOC_ADC_DIGI_RESULT_BYTES) <= bytes_read;
             offset += SOC_ADC_DIGI_RESULT_BYTES) {
            const auto* raw = reinterpret_cast<const adc_digi_output_data_t*>(ae_adc_read_buffer_.data() + offset);
#if CONFIG_IDF_TARGET_ESP32S3
            const std::uint32_t channel = raw->type2.channel;
            const std::uint16_t data = static_cast<std::uint16_t>(raw->type2.data);
#else
            const std::uint32_t channel = raw->type1.channel;
            const std::uint16_t data = static_cast<std::uint16_t>(raw->type1.data);
#endif
            if (channel != static_cast<std::uint32_t>(config_.ae_adc_channel)) {
                continue;
            }

            ae_sample_window_[ae_sample_count_] = data;
            ++ae_sample_count_;
            if (ae_sample_count_ >= config_.spectral_window_size) {
                ProcessAeSpectralWindow();
                ae_sample_count_ = 0U;
            }
        }
    }
#else
    vTaskDelete(nullptr);
#endif
}

} // namespace monitor
