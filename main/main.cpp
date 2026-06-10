/// @file main.cpp
/// @brief Application entry point for the branch monitoring node.
/// @details Boot sequence:
/// 1. Create default ESP event loop
/// 2. Initialize I2C bus (400kHz) for LSM6DS3TR IMU
/// 3. Initialize and configure the IMU with calibration biases
/// 4. Mount microSD card (SDMMC 1-bit or SDSPI)
/// 5. Initialize MQTT core (WiFi + MQTT client)
/// 6. Start Logger task (SD CSV + MQTT publishing)
/// 7. Start Monitor task (IMU sampling + disturbance detection)
/// 8. Sync NTP time
/// 9. Optionally start HTTP dashboard
/// 10. Run startup verification (IMU read, SD write, MQTT publish)
/// 11. Delete app_main and let FreeRTOS tasks run
/// @ingroup main

#include <cstdint>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include "esp_log_level.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "sdmmc_cmd.h"

#include "pins.hpp"
#include "monitor.hpp"
#include "calibration.hpp"
#include "logger.hpp"
#include "logger_internal.hpp"
#include "dashboard.hpp"
#include "verify.hpp"

namespace {

constexpr std::uint8_t kImuAddress = 0x6A;
constexpr std::uint32_t kI2cTimeoutMs = 100U;
constexpr std::size_t kMaxI2cWrite = 8U;
constexpr std::uint32_t kSdAllocUnit = 16U * 1024U;
constexpr int kSdMaxFiles = 5;
static const char* kAppTag = "APP";
#if CONFIG_APP_VERIFY_ENABLE
static const char* kVerifyTag = "VERIFY";
#endif

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

bool ImuReadReg(uint8_t reg, uint8_t* data, size_t len) {
    if (g_imu_dev == nullptr) {
        return false;
    }
    return i2c_master_transmit_receive(g_imu_dev, &reg, 1U, data, len, kI2cTimeoutMs) == ESP_OK;
}

bool ImuWriteReg(uint8_t reg, const uint8_t* data, size_t len) {
    if (g_imu_dev == nullptr || len > kMaxI2cWrite) {
        return false;
    }

    std::array<uint8_t, kMaxI2cWrite + 1U> buffer{};
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
        ESP_LOGE(kAppTag, "SDMMC mount failed: %s", esp_err_to_name(err));
        return false;
    }
#else
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_config_t bus_cfg{};
    bus_cfg.mosi_io_num = pins::SD_CMD_DI;
    bus_cfg.miso_io_num = pins::SD_DAT0_DO;
    bus_cfg.sclk_io_num = pins::SD_CLK;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = static_cast<int>(kSdAllocUnit);

    esp_err_t err = spi_bus_initialize(static_cast<spi_host_device_t>(host.slot),
                                       &bus_cfg,
                                       SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(kAppTag, "SDSPI bus init failed: %s", esp_err_to_name(err));
        return false;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = pins::SD_DAT3_CS;
    slot_config.host_id = static_cast<spi_host_device_t>(host.slot);

    err = esp_vfs_fat_sdspi_mount(CONFIG_APP_SD_MOUNT_POINT,
                                  &host,
                                  &slot_config,
                                  &mount_config,
                                  &g_sd_card);
    if (err != ESP_OK) {
        ESP_LOGE(kAppTag, "SDSPI mount failed: %s", esp_err_to_name(err));
        spi_bus_free(static_cast<spi_host_device_t>(host.slot));
        return false;
    }
#endif

    return true;
}

void LogHeapDiagnostics(const char* stage) {
    const std::uint32_t free_internal = static_cast<std::uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    const std::uint32_t min_free = static_cast<std::uint32_t>(esp_get_minimum_free_heap_size());
    const std::uint32_t largest_block = static_cast<std::uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    ESP_LOGI(kAppTag, "heap %s free=%lu min_free=%lu largest_block=%lu",
             stage,
             static_cast<unsigned long>(free_internal),
             static_cast<unsigned long>(min_free),
             static_cast<unsigned long>(largest_block));
}

} // namespace

extern "C" void app_main(void) {
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_parse", ESP_LOG_INFO);
    esp_log_level_set("httpd_uri", ESP_LOG_INFO);
    esp_log_level_set("httpd_sess", ESP_LOG_INFO);
    esp_log_level_set("httpd", ESP_LOG_INFO);
    esp_log_level_set("event", ESP_LOG_INFO);

    const esp_err_t loop_err = esp_event_loop_create_default();
    if (loop_err != ESP_OK && loop_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(kAppTag, "Default event loop creation failed: %s", esp_err_to_name(loop_err));
        return;
    }

    if (!InitImuI2c()) {
        ESP_LOGE(kAppTag, "I2C init failed");
        return;
    }

    sensor::Lsm6ds3::Config imu_cfg{};
    if (!MapImuOdr(static_cast<std::uint32_t>(CONFIG_MONITOR_IMU_RATE_HZ),
                   imu_cfg.imu_config.accel_odr,
                   imu_cfg.imu_config.gyro_odr)) {
        ESP_LOGE(kAppTag, "Unsupported IMU rate %d Hz", CONFIG_MONITOR_IMU_RATE_HZ);
        return;
    }
    imu_cfg.read_cb = ImuReadReg;
    imu_cfg.write_cb = ImuWriteReg;

    monitor::MonitorConfig monitor_cfg{};
    monitor_cfg.ae_gpio_pin = static_cast<std::int32_t>(pins::AE_GPIO_PIN);
    monitor_cfg.ae_adc_channel = static_cast<std::int32_t>(ADC_CHANNEL_3);

    static monitor::Monitor monitor{imu_cfg, monitor_cfg};
    if (!monitor.Init()) {
        ESP_LOGE(kAppTag, "Monitor init failed");
        return;
    }

    {
        calibration::CalibrationBias bias{};
        bias.ax =  0.014925f;
        bias.ay = -0.010015f;
        bias.az =  0.010312f;
        bias.gx =  1.096412f;
        bias.gy = -2.593744f;
        bias.gz =  0.414028f;
        monitor.SetCalibrationBiases(bias);
    }

    if (!InitSdCard()) {
        ESP_LOGE(kAppTag, "SD card init failed");
        return;
    }

    static logger::Logger logger{};
    logger::Logger::Config logger_cfg{};
    logger_cfg.sd_mount_point = CONFIG_APP_SD_MOUNT_POINT;

    if (!logger::mqtt::InitCore()) {
        ESP_LOGW(kAppTag, "InitCore failed");
    }

    if (!logger.Init(logger_cfg)) {
        ESP_LOGE(kAppTag, "Logger init failed");
        return;
    }

    bool all_critical_started = true;

    if (!logger.Start()) {
        all_critical_started = false;
    }

    if (!monitor.Start()) {
        all_critical_started = false;
    }

    LogHeapDiagnostics("post_tasks_pre_wifi");

    if (!logger::mqtt::StartWifi()) {
        ESP_LOGW(kAppTag, "StartWifi failed, continuing boot");
    }

    ESP_LOGI(kAppTag, "Synchronizing system time via NTP at startup...");
    if (logger::mqtt::SyncTimeOnce()) {
        ESP_LOGI(kAppTag, "Startup NTP time synchronization successful");
    } else {
        ESP_LOGW(kAppTag, "Startup NTP time synchronization failed or timed-out. Continuing boot.");
    }

    LogHeapDiagnostics("post_ntp");

#if CONFIG_DASHBOARD_ENABLE
    {
        LogHeapDiagnostics("pre_dashboard");
        static dashboard::Dashboard dashboard{monitor, logger};
        dashboard::Dashboard::Config dash_cfg{};
        dash_cfg.port = CONFIG_DASHBOARD_PORT;
        dash_cfg.enabled = true;
        const esp_err_t dash_err = dashboard.Start(dash_cfg);
        if (dash_err != ESP_OK) {
            ESP_LOGE(kAppTag, "Dashboard start failed");
        }
        LogHeapDiagnostics("post_dashboard");
    }
#endif

#if CONFIG_APP_VERIFY_ENABLE
    ESP_LOGI(kVerifyTag, "Verification start");
    verify::LogRuntimeDiagnostics("startup", monitor.GetTaskHandle(), logger.GetTaskHandle());

    sensor::lsm6ds3::Value gyro{};
    sensor::lsm6ds3::Value accel{};
    if (monitor.ReadImuSample(gyro, accel)) {
        ESP_LOGI(kVerifyTag,
                 "IMU sample accel=(%.3f, %.3f, %.3f) gyro=(%.3f, %.3f, %.3f)",
                 accel.x, accel.y, accel.z,
                 gyro.x, gyro.y, gyro.z);
    } else {
        ESP_LOGE(kVerifyTag, "IMU sample read failed");
    }

    static_cast<void>(verify::VerifySdStorage());
    static_cast<void>(verify::VerifyMqtt(logger));

    static_cast<void>(verify::VerifyMonitorOutput(logger));
    verify::LogRuntimeDiagnostics("post-verify", monitor.GetTaskHandle(), logger.GetTaskHandle());
    ESP_LOGI(kVerifyTag,
             "backpressure monitor_result_drop=%lu monitor_failure_drop=%lu logger_drop=%lu logger_param_drop=%lu logger_failure_drop=%lu",
             static_cast<unsigned long>(monitor.DroppedResultEvents()),
             static_cast<unsigned long>(monitor.DroppedFailureEvents()),
             static_cast<unsigned long>(logger.DroppedEvents()),
             static_cast<unsigned long>(logger.DroppedParameters()),
             static_cast<unsigned long>(logger.DroppedFailures()));
    ESP_LOGI(kVerifyTag, "Verification end");
#endif

    if (all_critical_started) {
        ESP_LOGI(kAppTag, "All tasks started, deleting app_main thread");
    } else {
        ESP_LOGE(kAppTag, "Critical task start failed, deleting app_main thread");
    }
    vTaskDelete(nullptr);
}
