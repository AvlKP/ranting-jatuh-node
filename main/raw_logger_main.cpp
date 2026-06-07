#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/sdmmc_host.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdmmc_cmd.h"

#include "pins.hpp"
#include "lsm6ds3.hpp"

namespace {

constexpr std::uint8_t kImuAddress = 0x6A;
constexpr std::uint32_t kI2cTimeoutMs = 100U;
constexpr std::size_t kMaxI2cWrite = 8U;
constexpr std::uint32_t kSdAllocUnit = 16U * 1024U;
constexpr int kSdMaxFiles = 5;
static const char* kTag = "RAWLOG";
constexpr std::uint32_t kHeartbeatSamples = 260U;

i2c_master_bus_handle_t g_i2c_bus = nullptr;
i2c_master_dev_handle_t g_imu_dev = nullptr;
sdmmc_card_t* g_sd_card = nullptr;

bool InitImuI2c() {
    i2c_master_bus_config_t bus_cfg{};
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.i2c_port = I2C_NUM_0;
    bus_cfg.scl_io_num = pins::IMU_SCL;
    bus_cfg.sda_io_num = pins::IMU_SDA;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;

    if (i2c_new_master_bus(&bus_cfg, &g_i2c_bus) != ESP_OK) {
        return false;
    }

    i2c_device_config_t dev_cfg{};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = kImuAddress;
    dev_cfg.scl_speed_hz = 400000;

    return i2c_master_bus_add_device(g_i2c_bus, &dev_cfg, &g_imu_dev) == ESP_OK;
}

bool ImuReadReg(std::uint8_t reg, std::uint8_t* data, std::size_t len) {
    if (g_imu_dev == nullptr) {
        return false;
    }
    return i2c_master_transmit_receive(g_imu_dev, &reg, 1U, data, len, kI2cTimeoutMs) == ESP_OK;
}

bool ImuWriteReg(std::uint8_t reg, const std::uint8_t* data, std::size_t len) {
    if (g_imu_dev == nullptr || len > kMaxI2cWrite) {
        return false;
    }

    std::array<std::uint8_t, kMaxI2cWrite + 1U> buffer{};
    buffer[0] = reg;
    for (std::size_t i = 0U; i < len; ++i) {
        buffer[i + 1U] = data[i];
    }

    return i2c_master_transmit(g_imu_dev, buffer.data(), len + 1U, kI2cTimeoutMs) == ESP_OK;
}

bool MapImuOdr(std::uint32_t rate_hz,
               sensor::lsm6ds3::AccelOdr& accel_odr,
               sensor::lsm6ds3::GyroOdr& gyro_odr) {
    switch (rate_hz) {
        case 26U:
            accel_odr = sensor::lsm6ds3::AccelOdr::ODR_26Hz;
            gyro_odr = sensor::lsm6ds3::GyroOdr::ODR_26Hz;
            return true;
        case 52U:
            accel_odr = sensor::lsm6ds3::AccelOdr::ODR_52Hz;
            gyro_odr = sensor::lsm6ds3::GyroOdr::ODR_52Hz;
            return true;
        case 104U:
            accel_odr = sensor::lsm6ds3::AccelOdr::ODR_104Hz;
            gyro_odr = sensor::lsm6ds3::GyroOdr::ODR_104Hz;
            return true;
        case 208U:
            accel_odr = sensor::lsm6ds3::AccelOdr::ODR_208Hz;
            gyro_odr = sensor::lsm6ds3::GyroOdr::ODR_208Hz;
            return true;
        default:
            return false;
    }
}

bool InitSdCard() {
    esp_vfs_fat_sdmmc_mount_config_t mount_config{};
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = kSdMaxFiles;
    mount_config.allocation_unit_size = kSdAllocUnit;

#if CONFIG_APP_SD_INTERFACE_SDMMC
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT;
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;
    slot_config.clk = pins::SD_CLK;
    slot_config.cmd = pins::SD_CMD_DI;
    slot_config.d0 = pins::SD_DAT0_DO;

    const esp_err_t err = esp_vfs_fat_sdmmc_mount(CONFIG_APP_SD_MOUNT_POINT,
                                                  &host,
                                                  &slot_config,
                                                  &mount_config,
                                                  &g_sd_card);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "SDMMC mount failed: %s", esp_err_to_name(err));
        return false;
    }
#else
    ESP_LOGE(kTag, "Only SDMMC interface is supported for raw logger");
    return false;
#endif

    return true;
}

std::uint32_t GetBootCounter() {
    std::uint32_t counter = 0U;
    nvs_handle_t handle = 0;
    if (nvs_open("rawlog", NVS_READWRITE, &handle) != ESP_OK) {
        return 0U;
    }
    nvs_get_u32(handle, "boot_count", &counter);
    ++counter;
    nvs_set_u32(handle, "boot_count", counter);
    nvs_commit(handle);
    nvs_close(handle);
    return counter;
}

} // namespace

extern "C" void app_main(void) {
    ESP_LOGI(kTag, "Raw IMU Logger starting");

    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err != ESP_OK && nvs_err != ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_LOGE(kTag, "NVS init failed: %s", esp_err_to_name(nvs_err));
        return;
    }

    if (!InitImuI2c()) {
        ESP_LOGE(kTag, "I2C init failed");
        return;
    }

    const std::uint32_t rate_hz = static_cast<std::uint32_t>(CONFIG_RAW_LOGGER_IMU_RATE_HZ);
    const std::uint32_t duration_s = static_cast<std::uint32_t>(CONFIG_RAW_LOGGER_DURATION_S);

    sensor::Lsm6ds3::Config imu_cfg{};
    if (!MapImuOdr(rate_hz, imu_cfg.imu_config.accel_odr, imu_cfg.imu_config.gyro_odr)) {
        ESP_LOGE(kTag, "Unsupported IMU rate %lu Hz", static_cast<unsigned long>(rate_hz));
        return;
    }
    imu_cfg.read_cb = ImuReadReg;
    imu_cfg.write_cb = ImuWriteReg;

    sensor::Lsm6ds3 imu{imu_cfg};
    if (!imu.init()) {
        ESP_LOGE(kTag, "IMU init failed");
        return;
    }

    if (!InitSdCard()) {
        ESP_LOGE(kTag, "SD card init failed");
        return;
    }

    char filepath[128];
    std::time_t t = std::time(nullptr);
    if (t > 1609459200) {
        std::strftime(filepath, sizeof(filepath),
                      CONFIG_APP_SD_MOUNT_POINT "/raw_log_%Y%m%d_%H%M%S.csv",
                      std::localtime(&t));
    } else {
        const std::uint32_t boot_id = GetBootCounter();
        std::snprintf(filepath, sizeof(filepath),
                      CONFIG_APP_SD_MOUNT_POINT "/raw_log_%lu.csv",
                      static_cast<unsigned long>(boot_id));
    }

    FILE* f = std::fopen(filepath, "w");
    if (f == nullptr) {
        ESP_LOGE(kTag, "Failed to create log file: %s", filepath);
        return;
    }

    const std::uint32_t period_ms = (1000U + rate_hz - 1U) / rate_hz;
    const TickType_t period_ticks = pdMS_TO_TICKS(period_ms);
    TickType_t last_wake = xTaskGetTickCount();
    std::uint64_t sample_count = 0ULL;
    const std::uint64_t max_samples = (duration_s > 0U)
        ? (static_cast<std::uint64_t>(duration_s) * static_cast<std::uint64_t>(rate_hz))
        : 0ULL;

    if (duration_s > 0U) {
        ESP_LOGI(kTag, "Recording %lus at %lu Hz (%llu samples) to %s",
                 static_cast<unsigned long>(duration_s),
                 static_cast<unsigned long>(rate_hz),
                 static_cast<unsigned long long>(max_samples),
                 filepath);
    } else {
        ESP_LOGI(kTag, "Recording indefinitely at %lu Hz to %s",
                 static_cast<unsigned long>(rate_hz),
                 filepath);
    }

    while (true) {
        vTaskDelayUntil(&last_wake, period_ticks);

        sensor::lsm6ds3::Value gyro{};
        sensor::lsm6ds3::Value accel{};

        if (!imu.read_accel_gyro(gyro, accel)) {
            ESP_LOGW(kTag, "IMU read failed at sample %llu",
                     static_cast<unsigned long long>(sample_count));
            continue;
        }

        float temp_c = 0.0f;
        if (!imu.read_temp(temp_c)) {
            ESP_LOGW(kTag, "Temperature read failed at sample %llu",
                     static_cast<unsigned long long>(sample_count));
        }

        std::fprintf(f, "%llu,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
                     static_cast<unsigned long long>(esp_timer_get_time()),
                     static_cast<double>(accel.x),
                     static_cast<double>(accel.y),
                     static_cast<double>(accel.z),
                     static_cast<double>(gyro.x),
                     static_cast<double>(gyro.y),
                     static_cast<double>(gyro.z),
                     static_cast<double>(temp_c));

        ++sample_count;

        if ((sample_count % static_cast<std::uint64_t>(kHeartbeatSamples)) == 0ULL) {
            std::fflush(f);
            fsync(fileno(f));
            ESP_LOGI(kTag, "%llu samples recorded",
                     static_cast<unsigned long long>(sample_count));
        }

        if (max_samples > 0ULL && sample_count >= max_samples) {
            break;
        }
    }

    std::fflush(f);
    fsync(fileno(f));
    std::fclose(f);

    ESP_LOGI(kTag, "Recording complete: %llu samples written to %s",
             static_cast<unsigned long long>(sample_count), filepath);

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }
}
