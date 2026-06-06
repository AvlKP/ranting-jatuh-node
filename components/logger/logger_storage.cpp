#include "logger_internal.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "esp_log.h"

#include "esp_timer.h"

#include "sdkconfig.h"
namespace logger::storage {

namespace {

constexpr std::size_t kPathMax = 128U;
const char* s_mount_point = nullptr;
static const char* kTag = "LOGGER_STORAGE";

constexpr std::uint64_t kSdCooldownUs = 5000000ULL;
std::atomic<bool> g_sd_healthy{true};
std::uint64_t g_sd_unhealthy_since_us{0U};

#if CONFIG_APP_DEBUG_CSV_LOGS
constexpr std::size_t kDebugBufferMax = 64U;
std::array<CsvLine, kDebugBufferMax> s_debug_buffer{};
std::size_t s_debug_buffer_head{0U};
std::size_t s_debug_buffer_count{0U};
#endif

const char* ParameterHeader() {
    return "timestamp_unix,timestamp_us,roll_mean,pitch_mean,roll_var,pitch_var,"
           "roll_pp_max,roll_pp_mean,pitch_pp_max,pitch_pp_mean,roll_zeta,pitch_zeta,"
           "natural_freq_hz,natural_freq_roll_hz,natural_freq_pitch_hz,state,sample_count\n";
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
        ESP_LOGE(kTag, "Open header file failed: %s errno=%d (%s)",
                 path,
                 errno,
                 std::strerror(errno));
        MarkSdUnhealthy();
        return false;
    }

    const std::size_t header_len = std::strlen(header);
    const std::size_t written = std::fwrite(header, 1U, header_len, file);
    std::fclose(file);
    if (written != header_len) {
        ESP_LOGE(kTag, "Header write failed: %s errno=%d (%s)",
                 path,
                 errno,
                 std::strerror(errno));
        MarkSdUnhealthy();
    }
    return written == header_len;
}

bool BuildParameterPath(const TimeInfo& time_info, char* path, std::size_t path_len) {
    if (path == nullptr || path_len == 0U) {
        return false;
    }

    if (time_info.valid) {
        const int len = std::snprintf(path,
                                      path_len,
                                      "%s/%s.csv",
                                      s_mount_point,
                                      time_info.date_yyyymmdd.data());
        return len > 0 && static_cast<std::size_t>(len) < path_len;
    }

    const int len = std::snprintf(path,
                                  path_len,
                                  "%s/unsync.csv",
                                  s_mount_point);
    return len > 0 && static_cast<std::size_t>(len) < path_len;
}

bool BuildFailurePath(char* path, std::size_t path_len) {
    if (path == nullptr || path_len == 0U) {
        return false;
    }

    const int len = std::snprintf(path,
                                  path_len,
                                  "%s/failure.csv",
                                  s_mount_point);
    return len > 0 && static_cast<std::size_t>(len) < path_len;
}

bool BuildDebugLogPath(char* path, std::size_t path_len) {
    if (path == nullptr || path_len == 0U) {
        return false;
    }

    const int len = std::snprintf(path,
                                  path_len,
                                  "%s/debug.csv",
                                  s_mount_point);
    return len > 0 && static_cast<std::size_t>(len) < path_len;
}

bool AppendLine(const char* path, const CsvLine& line) {
    FILE* file = std::fopen(path, "a");
    if (file == nullptr) {
        ESP_LOGE(kTag, "Open append failed: %s errno=%d (%s)",
                 path,
                 errno,
                 std::strerror(errno));
        MarkSdUnhealthy();
        return false;
    }

    const std::size_t written = std::fwrite(line.buffer.data(), 1U, line.length, file);
    std::fclose(file);
    if (written != line.length) {
        ESP_LOGE(kTag, "Append write failed: %s errno=%d (%s)",
                 path,
                 errno,
                 std::strerror(errno));
        MarkSdUnhealthy();
    }
    return written == line.length;
}

} // namespace

void MarkSdUnhealthy() noexcept {
    g_sd_unhealthy_since_us = static_cast<std::uint64_t>(esp_timer_get_time());
    g_sd_healthy.store(false, std::memory_order_release);
}

bool IsSdHealthy() noexcept {
    if (g_sd_healthy.load(std::memory_order_acquire)) {
        return true;
    }
    const std::uint64_t now_us = static_cast<std::uint64_t>(esp_timer_get_time());
    if (now_us - g_sd_unhealthy_since_us >= kSdCooldownUs) {
        g_sd_healthy.store(true, std::memory_order_release);
        return true;
    }
    return false;
}

void SetMountPoint(const char* mount_point) noexcept {
    s_mount_point = mount_point;
}

bool AppendParameter(const TimeInfo& time_info, const CsvLine& line) noexcept {
    if (s_mount_point == nullptr || std::strlen(s_mount_point) == 0U) {
        ESP_LOGE(kTag, "SD mount point not set");
        return false;
    }
    if (!IsSdHealthy()) {
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
    if (s_mount_point == nullptr || std::strlen(s_mount_point) == 0U) {
        ESP_LOGE(kTag, "SD mount point not set");
        return false;
    }
    if (!IsSdHealthy()) {
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

bool ResetDebugLog() noexcept {
#if CONFIG_APP_DEBUG_CSV_LOGS
    if (s_mount_point == nullptr || std::strlen(s_mount_point) == 0U) {
        return false;
    }
    char path[kPathMax]{};
    if (!BuildDebugLogPath(path, sizeof(path))) {
        return false;
    }
    FILE* file = std::fopen(path, "w");
    if (file == nullptr) {
        ESP_LOGE(kTag, "Open debug log for write failed: %s errno=%d (%s)",
                 path, errno, std::strerror(errno));
        MarkSdUnhealthy();
        return false;
    }
    const char* header = "timestamp_ms,accel_x,accel_y,accel_z,tilt_x,tilt_y,tilt_z,state\n";
    const std::size_t header_len = std::strlen(header);
    const std::size_t written = std::fwrite(header, 1U, header_len, file);
    std::fclose(file);
    return written == header_len;
#else
    return true;
#endif
}

bool AppendDebugLog(const CsvLine& line) noexcept {
#if CONFIG_APP_DEBUG_CSV_LOGS
    if (s_mount_point == nullptr || std::strlen(s_mount_point) == 0U) {
        return false;
    }
    if (s_debug_buffer_count >= kDebugBufferMax) {
        return false;
    }
    const std::size_t tail = (s_debug_buffer_head + s_debug_buffer_count) % kDebugBufferMax;
    s_debug_buffer[tail] = line;
    ++s_debug_buffer_count;
    return true;
#else
    static_cast<void>(line);
    return true;
#endif
}

bool FlushDebugLog() noexcept {
#if CONFIG_APP_DEBUG_CSV_LOGS
    if (s_debug_buffer_count == 0U) {
        return true;
    }
    if (!IsSdHealthy()) {
        return false;
    }
    if (s_mount_point == nullptr || std::strlen(s_mount_point) == 0U) {
        s_debug_buffer_head = 0U;
        s_debug_buffer_count = 0U;
        return false;
    }
    char path[kPathMax]{};
    if (!BuildDebugLogPath(path, sizeof(path))) {
        return false;
    }
    FILE* file = std::fopen(path, "a");
    if (file == nullptr) {
        ESP_LOGE(kTag, "Open debug log for flush failed: %s errno=%d (%s)",
                 path, errno, std::strerror(errno));
        MarkSdUnhealthy();
        return false;
    }
    bool success = true;
    for (std::size_t i = 0U; i < s_debug_buffer_count; ++i) {
        const std::size_t idx = (s_debug_buffer_head + i) % kDebugBufferMax;
        const CsvLine& buf_line = s_debug_buffer[idx];
        const std::size_t written = std::fwrite(buf_line.buffer.data(), 1U, buf_line.length, file);
        if (written != buf_line.length) {
            ESP_LOGE(kTag, "Flush write failed: errno=%d (%s)", errno, std::strerror(errno));
            MarkSdUnhealthy();
            success = false;
            break;
        }
    }
    std::fclose(file);
    if (success) {
        s_debug_buffer_head = 0U;
        s_debug_buffer_count = 0U;
    }
    return success;
#else
    return true;
#endif
}

} // namespace logger::storage
