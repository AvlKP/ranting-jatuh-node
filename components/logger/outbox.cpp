/// @file outbox.cpp
/// @brief SD-backed upload queue implementation.
/// @ingroup logger

#include "outbox.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <dirent.h>

#include "esp_log.h"
#include "sdkconfig.h"

namespace logger::outbox {

namespace {

static const char* kTag = "OUTBOX";
constexpr std::uint32_t kMinValidEpoch = 1672531200U;
constexpr std::uint32_t kPublishPeriodSec =
    static_cast<std::uint32_t>(CONFIG_LOGGER_WIFI_PERIOD_HOURS) * 3600U;

const char* s_mount_point = nullptr;
char s_pending_dir[kPathMax]{};
char s_sent_dir[kPathMax]{};

char s_current_params_file[kMaxFilenameLen]{};
std::uint32_t s_current_params_epoch{0U};

// Reusable buffer for temporary path construction to minimize task stack usage.
char s_path_buf[kPathMax]{};

bool BuildOutboxPath(const char* subdir, char* out_path, std::size_t out_len) {
    if (s_mount_point == nullptr) {
        return false;
    }
    const int len = std::snprintf(out_path, out_len, "%s/outbox/%s", s_mount_point, subdir);
    return len > 0 && static_cast<std::size_t>(len) < out_len;
}

bool BuildPendingPath(const char* filename, char* out_path, std::size_t out_len) {
    const int len = std::snprintf(out_path, out_len, "%s/%s", s_pending_dir, filename);
    return len > 0 && static_cast<std::size_t>(len) < out_len;
}

bool BuildSentPath(const char* filename, char* out_path, std::size_t out_len) {
    const int len = std::snprintf(out_path, out_len, "%s/%s", s_sent_dir, filename);
    return len > 0 && static_cast<std::size_t>(len) < out_len;
}

bool EnsureDir(const char* path) {
    struct stat st{};
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        return true;
    }
    if (mkdir(path, 0755) != 0) {
        ESP_LOGE(kTag, "mkdir %s failed: errno=%d (%s)", path, errno, std::strerror(errno));
        return false;
    }
    return true;
}

std::uint32_t GetCurrentEpoch() {
    std::time_t now = 0;
    std::time(&now);
    if (static_cast<std::uint32_t>(now) < kMinValidEpoch) {
        return 0U;
    }
    return static_cast<std::uint32_t>(now);
}

std::uint32_t EpochForPeriod(std::uint32_t epoch) {
    if (epoch == 0U || kPublishPeriodSec == 0U) {
        return epoch;
    }
    return epoch - (epoch % kPublishPeriodSec);
}

bool OpenCurrentParamsFile(std::uint32_t epoch) {
    if (s_current_params_file[0] != '\0') {
        return true;
    }

    const std::uint32_t period_epoch = EpochForPeriod(epoch);
    std::snprintf(s_current_params_file, sizeof(s_current_params_file),
                  "params_%lu.jsonl", static_cast<unsigned long>(period_epoch));
    s_current_params_epoch = period_epoch;

    char path[kPathMax]{};
    if (!BuildPendingPath(s_current_params_file, path, sizeof(path))) {
        return false;
    }

    FILE* file = std::fopen(path, "a");
    if (file == nullptr) {
        ESP_LOGE(kTag, "Open params file failed: %s errno=%d", path, errno);
        return false;
    }
    std::fclose(file);
    return true;
}

bool NeedsRotation(std::uint32_t epoch) {
    if (s_current_params_file[0] == '\0') {
        return true;
    }
    const std::uint32_t period_epoch = EpochForPeriod(epoch);
    return period_epoch != s_current_params_epoch;
}

bool WriteLine(const char* path, const char* line) {
    FILE* file = std::fopen(path, "a");
    if (file == nullptr) {
        ESP_LOGE(kTag, "Open append failed: %s errno=%d", path, errno);
        return false;
    }

    const std::size_t len = std::strlen(line);
    const std::size_t written = std::fwrite(line, 1U, len, file);
    std::fclose(file);

    if (written != len) {
        ESP_LOGE(kTag, "Write failed: %s errno=%d", path, errno);
        return false;
    }
    return true;
}

bool MoveFile(const char* src, const char* dst) {
    if (rename(src, dst) != 0) {
        ESP_LOGE(kTag, "Rename %s -> %s failed: errno=%d", src, dst, errno);
        return false;
    }
    return true;
}

bool DeleteFile(const char* path) {
    if (remove(path) != 0) {
        ESP_LOGW(kTag, "Remove %s failed: errno=%d", path, errno);
        return false;
    }
    return true;
}

int CompareFileEntry(const void* a, const void* b) {
    const auto* fa = static_cast<const FileEntry*>(a);
    const auto* fb = static_cast<const FileEntry*>(b);

    if (fa->is_failure && !fb->is_failure) {
        return -1;
    }
    if (!fa->is_failure && fb->is_failure) {
        return 1;
    }
    return std::strcmp(fa->filename.data(), fb->filename.data());
}

bool IsFailureFile(const char* name) {
    return std::strncmp(name, "failure_", 8U) == 0;
}

bool IsParamsFile(const char* name) {
    return std::strncmp(name, "params_", 7U) == 0;
}

} // namespace

bool Init(const char* mount_point) noexcept {
    if (mount_point == nullptr || std::strlen(mount_point) == 0U) {
        ESP_LOGE(kTag, "SD mount point not set");
        return false;
    }

    s_mount_point = mount_point;

    BuildOutboxPath("pending", s_pending_dir, sizeof(s_pending_dir));
    BuildOutboxPath("sent", s_sent_dir, sizeof(s_sent_dir));

    char outbox_dir[kPathMax]{};
    BuildOutboxPath("", outbox_dir, sizeof(outbox_dir));
    if (outbox_dir[0] != '\0') {
        outbox_dir[std::strlen(outbox_dir) - 1U] = '\0';
    }

    if (!EnsureDir(outbox_dir)) {
        return false;
    }
    if (!EnsureDir(s_pending_dir)) {
        return false;
    }
    if (!EnsureDir(s_sent_dir)) {
        return false;
    }

    ESP_LOGI(kTag, "Outbox initialized: %s", outbox_dir);
    return true;
}

bool AppendParameter(const char* json_line) noexcept {
    if (json_line == nullptr || s_mount_point == nullptr) {
        return false;
    }

    const std::uint32_t epoch = GetCurrentEpoch();

    if (NeedsRotation(epoch)) {
        s_current_params_file[0] = '\0';
        s_current_params_epoch = 0U;
    }

    if (!OpenCurrentParamsFile(epoch)) {
        return false;
    }

    char* path = s_path_buf;
    if (!BuildPendingPath(s_current_params_file, path, kPathMax)) {
        return false;
    }

    return WriteLine(path, json_line);
}

bool AppendFailure(const char* json_line) noexcept {
    if (json_line == nullptr || s_mount_point == nullptr) {
        return false;
    }

    const std::uint32_t epoch = GetCurrentEpoch();
    char filename[kMaxFilenameLen]{};
    std::snprintf(filename, sizeof(filename), "failure_%lu.jsonl",
                  static_cast<unsigned long>(epoch));

    char* path = s_path_buf;
    if (!BuildPendingPath(filename, path, kPathMax)) {
        return false;
    }

    return WriteLine(path, json_line);
}

bool RotateParameterFile() noexcept {
    s_current_params_file[0] = '\0';
    s_current_params_epoch = 0U;
    return true;
}

bool GetPendingFiles(FileEntry* out_entries, std::size_t& out_count) noexcept {
    if (out_entries == nullptr) {
        out_count = 0U;
        return false;
    }

    out_count = 0U;

    DIR* dir = opendir(s_pending_dir);
    if (dir == nullptr) {
        ESP_LOGW(kTag, "opendir %s failed: errno=%d", s_pending_dir, errno);
        return false;
    }

    struct dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr && out_count < kMaxPendingFiles) {
        if (entry->d_type != DT_REG) {
            continue;
        }

        const char* name = entry->d_name;
        if (!IsFailureFile(name) && !IsParamsFile(name)) {
            continue;
        }

        std::strncpy(out_entries[out_count].filename.data(), name, kMaxFilenameLen - 1U);
        out_entries[out_count].filename[kMaxFilenameLen - 1U] = '\0';
        out_entries[out_count].is_failure = IsFailureFile(name);
        ++out_count;
    }

    closedir(dir);

    if (out_count > 1U) {
        qsort(out_entries, out_count, sizeof(FileEntry), CompareFileEntry);
    }

    return true;
}

bool MarkSent(const char* filename) noexcept {
    if (filename == nullptr) {
        return false;
    }

    char src[kPathMax]{};
    char dst[kPathMax]{};
    if (!BuildPendingPath(filename, src, sizeof(src))) {
        return false;
    }
    if (!BuildSentPath(filename, dst, sizeof(dst))) {
        return false;
    }

    return MoveFile(src, dst);
}

bool PruneSent(std::size_t max_keep) noexcept {
    if (max_keep == 0U) {
        return true;
    }

    DIR* dir = opendir(s_sent_dir);
    if (dir == nullptr) {
        ESP_LOGW(kTag, "opendir %s failed: errno=%d", s_sent_dir, errno);
        return false;
    }

    struct {
        char name[kMaxFilenameLen]{};
        std::uint32_t mtime{0U};
    } files[kMaxPendingFiles]{};
    std::size_t count = 0U;

    struct dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr && count < kMaxPendingFiles) {
        if (entry->d_type != DT_REG) {
            continue;
        }

        std::strncpy(files[count].name, entry->d_name, kMaxFilenameLen - 1U);
        files[count].name[kMaxFilenameLen - 1U] = '\0';

        char full_path[kPathMax]{};
        if (BuildSentPath(entry->d_name, full_path, sizeof(full_path))) {
            struct stat st{};
            if (stat(full_path, &st) == 0) {
                files[count].mtime = static_cast<std::uint32_t>(st.st_mtime);
            }
        }
        ++count;
    }
    closedir(dir);

    if (count <= max_keep) {
        return true;
    }

    for (std::size_t i = 0U; i < count - 1U; ++i) {
        for (std::size_t j = i + 1U; j < count; ++j) {
            if (files[i].mtime > files[j].mtime) {
                const auto tmp = files[i];
                files[i] = files[j];
                files[j] = tmp;
            }
        }
    }

    const std::size_t to_delete = count - max_keep;
    for (std::size_t i = 0U; i < to_delete; ++i) {
        char path[kPathMax]{};
        if (BuildSentPath(files[i].name, path, sizeof(path))) {
            DeleteFile(path);
            ESP_LOGI(kTag, "Pruned sent file: %s", files[i].name);
        }
    }

    return true;
}

} // namespace logger::outbox
