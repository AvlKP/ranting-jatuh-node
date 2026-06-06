#include "verify.hpp"
#include "logger_internal.hpp"

#include <cstdint>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"

namespace verify {

namespace {

static const char* kVerifyTag = "VERIFY";
constexpr std::uint32_t kVerifyReadBuffer = 32U;

} // namespace

void LogStackHighWatermark(const char* stage) {
    const UBaseType_t words = uxTaskGetStackHighWaterMark(nullptr);
    const std::uint32_t bytes = static_cast<std::uint32_t>(words) * sizeof(StackType_t);
    ESP_LOGI(kVerifyTag, "%s stack high-water: %u bytes", stage, bytes);
}

bool VerifySdStorage() {
    std::array<char, 96U> path{};
    const int len = std::snprintf(path.data(),
                                  path.size(),
                                  "%s/verify.txt",
                                  CONFIG_APP_SD_MOUNT_POINT);
    if (len <= 0 || static_cast<std::size_t>(len) >= path.size()) {
        ESP_LOGE(kVerifyTag, "Verify path build failed");
        return false;
    }

    const char* payload = "verify_ok";
    const std::size_t payload_len = std::strlen(payload);

    FILE* file = std::fopen(path.data(), "w");
    if (file == nullptr) {
        ESP_LOGE(kVerifyTag, "Verify write open failed: %s errno=%d (%s)",
                 path.data(),
                 errno,
                 std::strerror(errno));
        return false;
    }
    const std::size_t written = std::fwrite(payload, 1U, payload_len, file);
    std::fclose(file);
    if (written != payload_len) {
        ESP_LOGE(kVerifyTag, "Verify write failed: %s errno=%d (%s)",
                 path.data(),
                 errno,
                 std::strerror(errno));
        return false;
    }

    file = std::fopen(path.data(), "r");
    if (file == nullptr) {
        ESP_LOGE(kVerifyTag, "Verify read open failed: %s errno=%d (%s)",
                 path.data(),
                 errno,
                 std::strerror(errno));
        return false;
    }

    std::array<char, kVerifyReadBuffer> buffer{};
    const std::size_t read = std::fread(buffer.data(), 1U, payload_len, file);
    std::fclose(file);
    if (read != payload_len || std::strncmp(buffer.data(), payload, payload_len) != 0) {
        ESP_LOGE(kVerifyTag, "Verify read mismatch: %s", path.data());
        return false;
    }

    ESP_LOGI(kVerifyTag, "Storage verify ok: %s", path.data());
    return true;
}

bool VerifyMqtt(logger::Logger& logger) {
    const char* topic = logger::mqtt::GetTopic("verify");
    if (topic == nullptr || std::strlen(topic) == 0U) {
        ESP_LOGE(kVerifyTag, "MQTT verify topic not set");
        return false;
    }

    const bool ok = logger.VerifyMqttPublish(topic, "verify_ok");
    if (ok) {
        ESP_LOGI(kVerifyTag, "MQTT verify publish ok: %s", topic);
    } else {
        ESP_LOGE(kVerifyTag, "MQTT verify publish failed: %s", topic);
    }
    return ok;
}

bool VerifyMonitorOutput(logger::Logger& logger) {
    const std::uint64_t start_us = static_cast<std::uint64_t>(esp_timer_get_time());
    const std::uint64_t timeout_us =
        static_cast<std::uint64_t>(CONFIG_APP_VERIFY_MONITOR_TIMEOUT_MS) * 1000ULL;

    while ((static_cast<std::uint64_t>(esp_timer_get_time()) - start_us) < timeout_us) {
        if (logger.HasMonitorResult()) {
            ESP_LOGI(kVerifyTag, "Monitor verify ok");
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGW(kVerifyTag, "Monitor verify timeout");
    return false;
}

} // namespace verify
