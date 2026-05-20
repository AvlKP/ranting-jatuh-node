#include "logger_internal.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
namespace logger::storage {

namespace {

constexpr std::size_t kPathMax = 128U;
const char* s_mount_point = nullptr;
static const char* kTag = "LOGGER_STORAGE";

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
        ESP_LOGE(kTag, "Open header file failed: %s errno=%d (%s)",
                 path,
                 errno,
                 std::strerror(errno));
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

bool AppendLine(const char* path, const CsvLine& line) {
    FILE* file = std::fopen(path, "a");
    if (file == nullptr) {
        ESP_LOGE(kTag, "Open append failed: %s errno=%d (%s)",
                 path,
                 errno,
                 std::strerror(errno));
        return false;
    }

    const std::size_t written = std::fwrite(line.buffer.data(), 1U, line.length, file);
    std::fclose(file);
    if (written != line.length) {
        ESP_LOGE(kTag, "Append write failed: %s errno=%d (%s)",
                 path,
                 errno,
                 std::strerror(errno));
    }
    return written == line.length;
}

} // namespace

void SetMountPoint(const char* mount_point) noexcept {
    s_mount_point = mount_point;
}

bool AppendParameter(const TimeInfo& time_info, const CsvLine& line) noexcept {
    if (s_mount_point == nullptr || std::strlen(s_mount_point) == 0U) {
        ESP_LOGE(kTag, "SD mount point not set");
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
