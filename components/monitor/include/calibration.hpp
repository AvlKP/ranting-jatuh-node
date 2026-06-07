#pragma once

#include "nvs.h"
#include "esp_err.h"

namespace calibration {

struct CalibrationBias {
    float ax{0.0f};
    float ay{0.0f};
    float az{0.0f};
    float gx{0.0f};
    float gy{0.0f};
    float gz{0.0f};
};

namespace Calibration {

inline esp_err_t ReadBiases(nvs_handle handle, CalibrationBias& bias) noexcept {
    std::size_t size = sizeof(CalibrationBias);
    esp_err_t err = nvs_get_blob(handle, "imu_bias", &bias, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        bias = CalibrationBias{};
        return ESP_OK;
    }
    return err;
}

inline esp_err_t WriteBiases(nvs_handle handle, const CalibrationBias& bias) noexcept {
    return nvs_set_blob(handle, "imu_bias", &bias, sizeof(CalibrationBias));
}

} // namespace Calibration
} // namespace calibration
