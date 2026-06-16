/// @file nvs_init.cpp
/// @brief Shared idempotent NVS initialization implementation.

#include "nvs_init.hpp"

#include "esp_log.h"
#include "nvs_flash.h"

namespace runtime {
namespace {

const char* kTag = "RUNTIME_NVS";
bool s_initialized = false;

} // namespace

esp_err_t EnsureNvsInitialized() noexcept {
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(kTag, "NVS init requires erase: %s", esp_err_to_name(err));
        const esp_err_t erase_err = nvs_flash_erase();
        if (erase_err != ESP_OK) {
            ESP_LOGE(kTag, "NVS erase failed: %s", esp_err_to_name(erase_err));
            return erase_err;
        }
        err = nvs_flash_init();
    }

    if (err == ESP_OK) {
        s_initialized = true;
        ESP_LOGI(kTag, "NVS initialized");
    } else {
        ESP_LOGE(kTag, "NVS init failed: %s", esp_err_to_name(err));
    }
    return err;
}

} // namespace runtime
