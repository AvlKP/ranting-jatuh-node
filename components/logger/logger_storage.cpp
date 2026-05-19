#include "logger_internal.hpp"

#include <cstdio>
#include <cstring>

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "pins.hpp"

namespace logger::storage {

namespace {

constexpr std::size_t kPathMax = 128U;
constexpr std::uint32_t kAllocUnit = 16U * 1024U;
constexpr int kMaxFiles = 5;

static bool s_initialized = false;
static sdmmc_card_t* s_card = nullptr;

const char* ParameterHeader() {
    return "timestamp_unix,timestamp_us,roll_mean,pitch_mean,roll_var,pitch_var,"
           "roll_pp_max,roll_pp_mean,pitch_pp_max,pitch_pp_mean,roll_zeta,pitch_zeta,"
           "natural_freq_hz,sample_count\n";
}

const char* FailureHeader() {
    return "timestamp_unix,timestamp_us,event\n";
}

bool EnsureFileHeader(const char* path, const char* header) {
    FILE* file = std::fopen(path, "r");
    if (file != nullptr) {
        std::fclose(file);
        return true;
    }

    file = std::fopen(path, "a");
    if (file == nullptr) {
        return false;
    }

    const std::size_t header_len = std::strlen(header);
    const std::size_t written = std::fwrite(header, 1U, header_len, file);
    std::fclose(file);
    return written == header_len;
}

bool BuildParameterPath(const TimeInfo& time_info, char* path, std::size_t path_len) {
    if (path == nullptr || path_len == 0U) {
        return false;
    }

    if (time_info.valid) {
        const int len = std::snprintf(path,
                                      path_len,
                                      "%s/params_%s.csv",
                                      CONFIG_LOGGER_SD_MOUNT_POINT,
                                      time_info.date_yyyymmdd.data());
        return len > 0 && static_cast<std::size_t>(len) < path_len;
    }

    const int len = std::snprintf(path,
                                  path_len,
                                  "%s/params_unsynced.csv",
                                  CONFIG_LOGGER_SD_MOUNT_POINT);
    return len > 0 && static_cast<std::size_t>(len) < path_len;
}

bool BuildFailurePath(char* path, std::size_t path_len) {
    if (path == nullptr || path_len == 0U) {
        return false;
    }

    const int len = std::snprintf(path,
                                  path_len,
                                  "%s/failure_events.csv",
                                  CONFIG_LOGGER_SD_MOUNT_POINT);
    return len > 0 && static_cast<std::size_t>(len) < path_len;
}

bool AppendLine(const char* path, const CsvLine& line) {
    FILE* file = std::fopen(path, "a");
    if (file == nullptr) {
        return false;
    }

    const std::size_t written = std::fwrite(line.buffer.data(), 1U, line.length, file);
    std::fclose(file);
    return written == line.length;
}

} // namespace

bool Init() noexcept {
    if (s_initialized) {
        return true;
    }

    esp_vfs_fat_sdmmc_mount_config_t mount_config{};
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = kMaxFiles;
    mount_config.allocation_unit_size = kAllocUnit;

#if CONFIG_LOGGER_SD_INTERFACE_SDMMC
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT;
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;
    slot_config.clk = pins::SD_CLK;
    slot_config.cmd = pins::SD_CMD_DI;
    slot_config.d0 = pins::SD_DAT0_DO;

    const esp_err_t err = esp_vfs_fat_sdmmc_mount(CONFIG_LOGGER_SD_MOUNT_POINT,
                                                  &host,
                                                  &slot_config,
                                                  &mount_config,
                                                  &s_card);
    if (err != ESP_OK) {
        std::printf("logger: sdmmc mount failed: %s\n", esp_err_to_name(err));
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
    bus_cfg.max_transfer_sz = static_cast<int>(kAllocUnit);

    esp_err_t err = spi_bus_initialize(static_cast<spi_host_device_t>(host.slot),
                                       &bus_cfg,
                                       SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        std::printf("logger: sdspi bus init failed: %s\n", esp_err_to_name(err));
        return false;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = pins::SD_DAT3_CS;
    slot_config.host_id = static_cast<spi_host_device_t>(host.slot);

    err = esp_vfs_fat_sdspi_mount(CONFIG_LOGGER_SD_MOUNT_POINT,
                                  &host,
                                  &slot_config,
                                  &mount_config,
                                  &s_card);
    if (err != ESP_OK) {
        std::printf("logger: sdspi mount failed: %s\n", esp_err_to_name(err));
        spi_bus_free(static_cast<spi_host_device_t>(host.slot));
        return false;
    }
#endif

    s_initialized = true;
    return true;
}

bool AppendParameter(const TimeInfo& time_info, const CsvLine& line) noexcept {
    if (!Init()) {
        return false;
    }

    char path[kPathMax]{};
    if (!BuildParameterPath(time_info, path, sizeof(path))) {
        return false;
    }
    if (!EnsureFileHeader(path, ParameterHeader())) {
        return false;
    }
    return AppendLine(path, line);
}

bool AppendFailure(const TimeInfo& time_info, const CsvLine& line) noexcept {
    if (!Init()) {
        return false;
    }

    static_cast<void>(time_info);
    char path[kPathMax]{};
    if (!BuildFailurePath(path, sizeof(path))) {
        return false;
    }
    if (!EnsureFileHeader(path, FailureHeader())) {
        return false;
    }
    return AppendLine(path, line);
}

} // namespace logger::storage
