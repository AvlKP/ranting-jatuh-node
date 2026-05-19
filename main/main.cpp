#include <cstdint>
#include <array>
#include <cstdio>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_adc/adc_oneshot.h"

#include "pins.hpp"
#include "monitor.hpp"
#include "logger.hpp"

namespace {

constexpr std::uint8_t kImuAddress = 0x6A;
constexpr std::uint32_t kI2cTimeoutMs = 100U;
constexpr std::size_t kMaxI2cWrite = 8U;

i2c_master_bus_handle_t g_i2c_bus = nullptr;
i2c_master_dev_handle_t g_imu_dev = nullptr;

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

} // namespace

extern "C" void app_main(void) {
    if (!InitImuI2c()) {
        std::printf("monitor: i2c init failed\n");
        return;
    }

    sensor::Lsm6ds3::Config imu_cfg{};
    if (!MapImuOdr(static_cast<std::uint32_t>(CONFIG_MONITOR_IMU_RATE_HZ),
                   imu_cfg.imu_config.accel_odr,
                   imu_cfg.imu_config.gyro_odr)) {
        std::printf("monitor: unsupported IMU rate %d Hz\n", CONFIG_MONITOR_IMU_RATE_HZ);
        return;
    }
    imu_cfg.read_cb = ImuReadReg;
    imu_cfg.write_cb = ImuWriteReg;

    monitor::MonitorConfig monitor_cfg{};
    monitor_cfg.ae_gpio_pin = static_cast<std::int32_t>(pins::AE_GPIO_PIN);
    monitor_cfg.ae_adc_channel = static_cast<std::int32_t>(ADC_CHANNEL_3);

    static monitor::Monitor monitor{imu_cfg, monitor_cfg};
    if (!monitor.Init()) {
        std::printf("monitor: init failed\n");
        return;
    }

    static logger::Logger logger{};
    if (!logger.Init()) {
        std::printf("logger: init failed\n");
        return;
    }
    if (!logger.Start()) {
        std::printf("logger: start failed\n");
        return;
    }
    monitor.RegisterCallback(&logger::Logger::MonitorCallback, &logger);
    monitor.RegisterFailureCallback(&logger::Logger::FailureCallback, &logger);

    const float dt_s = 1.0f / static_cast<float>(CONFIG_MONITOR_IMU_RATE_HZ);
    const std::uint32_t rate_hz = static_cast<std::uint32_t>(CONFIG_MONITOR_IMU_RATE_HZ);
    const std::uint32_t period_ms = (1000U + rate_hz - 1U) / rate_hz;
    const TickType_t period_ticks = pdMS_TO_TICKS(period_ms);

    while (true) {
        if (!monitor.Update(dt_s)) {
            std::printf("monitor: update failed\n");
        }
        vTaskDelay(period_ticks);
    }
}
