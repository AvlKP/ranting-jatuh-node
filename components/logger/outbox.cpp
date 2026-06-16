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

#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

namespace logger::outbox {

namespace {

static const char* kTag = "OUTBOX";
constexpr std::uint32_t kMinValidEpoch = 1672531200U;
constexpr std::uint32_t kPublishPeriodSec =
    static_cast<std::uint32_t>(CONFIG_LOGGER_WIFI_PERIOD_HOURS) * 60U;

const char* s_mount_point = nullptr;
char s_pending_dir[kPathMax]{};
char s_sent_dir[kPathMax]{};

char s_current_params_file[kMaxFilenameLen]{};
std::uint32_t s_current_params_epoch{0U};
std::uint32_t s_boot_id{0U};
std::uint32_t s_name_sequence{0U};

// Reusable buffer for temporary path construction to minimize task stack usage.
char s_path_buf[kPathMax]{};

StaticSemaphore_t s_mutex_buffer{};
SemaphoreHandle_t s_mutex = nullptr;

class OutboxLock {
public:
    OutboxLock() noexcept {
        if (s_mutex != nullptr) {
            locked_ = xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE;
        }
    }

    ~OutboxLock() {
        if (locked_) {
            xSemaphoreGive(s_mutex);
        }
    }

    OutboxLock(const OutboxLock&) = delete;
    OutboxLock& operator=(const OutboxLock&) = delete;

    [[nodiscard]] bool locked() const noexcept { return locked_; }

private:
    bool locked_{false};
};

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

bool MakeActiveFilename(char* out_name, std::size_t out_len);

bool OpenCurrentParamsFile(std::uint32_t epoch) {
    if (s_current_params_file[0] != '\0') {
        return true;
    }

    if (!MakeActiveFilename(s_current_params_file, sizeof(s_current_params_file))) {
        return false;
    }
    s_current_params_epoch = EpochForPeriod(epoch);

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
    static_cast<void>(epoch);
    return s_current_params_file[0] == '\0';
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

bool FileExists(const char* path) {
    struct stat st{};
    return stat(path, &st) == 0;
}

std::uint32_t NextSequence() {
    ++s_name_sequence;
    if (s_name_sequence == 0U) {
        ++s_name_sequence;
    }
    return s_name_sequence;
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

bool IsActiveParamsFile(const char* name) {
    return std::strncmp(name, "params_active_", 14U) == 0;
}

bool ClassifyPendingFilename(const char* name, bool& out_is_failure) {
    if (name == nullptr || IsActiveParamsFile(name)) {
        return false;
    }
    if (IsFailureFile(name)) {
        out_is_failure = true;
        return true;
    }
    if (IsParamsFile(name)) {
        out_is_failure = false;
        return true;
    }
    return false;
}

bool FormatUploadFilename(const char* prefix,
                          std::uint32_t epoch,
                          std::uint32_t boot_id,
                          std::uint32_t sequence,
                          char* out_name,
                          std::size_t out_len) {
    if (prefix == nullptr || out_name == nullptr) {
        return false;
    }
    const int len = std::snprintf(out_name,
                                  out_len,
                                  "%s_%lu_%08lx_%06lu.jsonl",
                                  prefix,
                                  static_cast<unsigned long>(epoch),
                                  static_cast<unsigned long>(boot_id),
                                  static_cast<unsigned long>(sequence));
    return len > 0 && static_cast<std::size_t>(len) < out_len;
}

bool FormatSentCollisionFilename(const char* filename,
                                 std::uint32_t sequence,
                                 char* out_name,
                                 std::size_t out_len) {
    if (filename == nullptr || out_name == nullptr) {
        return false;
    }

    char stem[kMaxFilenameLen]{};
    std::strncpy(stem, filename, sizeof(stem) - 1U);
    stem[sizeof(stem) - 1U] = '\0';
    char* ext = std::strstr(stem, ".jsonl");
    if (ext != nullptr) {
        *ext = '\0';
    }

    const int len = std::snprintf(out_name,
                                  out_len,
                                  "%s_sent_%06lu.jsonl",
                                  stem,
                                  static_cast<unsigned long>(sequence));
    return len > 0 && static_cast<std::size_t>(len) < out_len;
}

bool MakeUniquePendingFilename(const char* prefix,
                               std::uint32_t epoch,
                               char* out_name,
                               std::size_t out_len) {
    for (std::uint32_t attempt = 0U; attempt < 32U; ++attempt) {
        const std::uint32_t seq = NextSequence();
        if (!FormatUploadFilename(prefix, epoch, s_boot_id, seq, out_name, out_len)) {
            return false;
        }
        if (!BuildPendingPath(out_name, s_path_buf, kPathMax)) {
            return false;
        }
        if (!FileExists(s_path_buf)) {
            return true;
        }
    }
    return false;
}

bool MakeActiveFilename(char* out_name, std::size_t out_len) {
    const std::uint32_t seq = NextSequence();
    const int len = std::snprintf(out_name,
                                  out_len,
                                  "params_active_%08lx_%06lu.jsonl",
                                  static_cast<unsigned long>(s_boot_id),
                                  static_cast<unsigned long>(seq));
    return len > 0 && static_cast<std::size_t>(len) < out_len;
}

bool MakeUniqueSentPath(const char* filename, char* out_path, std::size_t out_len) {
    if (!BuildSentPath(filename, out_path, out_len)) {
        return false;
    }
    if (!FileExists(out_path)) {
        return true;
    }

    for (std::uint32_t attempt = 0U; attempt < 32U; ++attempt) {
        char unique[kMaxFilenameLen]{};
        const std::uint32_t seq = NextSequence();
        if (!FormatSentCollisionFilename(filename, seq, unique, sizeof(unique))) {
            return false;
        }
        if (!BuildSentPath(unique, out_path, out_len)) {
            return false;
        }
        if (!FileExists(out_path)) {
            return true;
        }
    }
    return false;
}

} // namespace

bool Init(const char* mount_point) noexcept {
    if (mount_point == nullptr || std::strlen(mount_point) == 0U) {
        ESP_LOGE(kTag, "SD mount point not set");
        return false;
    }

    s_mount_point = mount_point;
    s_boot_id = esp_random() ^ static_cast<std::uint32_t>(esp_timer_get_time());
    s_name_sequence = 0U;
    if (s_mutex == nullptr) {
        s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_buffer);
        if (s_mutex == nullptr) {
            ESP_LOGE(kTag, "Outbox mutex init failed");
            return false;
        }
    }

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

    OutboxLock lock;
    if (!lock.locked()) {
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

    OutboxLock lock;
    if (!lock.locked()) {
        return false;
    }

    const std::uint32_t epoch = GetCurrentEpoch();
    char filename[kMaxFilenameLen]{};
    if (!MakeUniquePendingFilename("failure", epoch, filename, sizeof(filename))) {
        return false;
    }

    char* path = s_path_buf;
    if (!BuildPendingPath(filename, path, kPathMax)) {
        return false;
    }

    return WriteLine(path, json_line);
}

bool SealParameterFile() noexcept {
    if (s_mount_point == nullptr) {
        return false;
    }

    OutboxLock lock;
    if (!lock.locked()) {
        return false;
    }

    if (s_current_params_file[0] == '\0') {
        return true;
    }

    char src[kPathMax]{};
    char dst[kPathMax]{};
    if (!BuildPendingPath(s_current_params_file, src, sizeof(src))) {
        return false;
    }

    struct stat st{};
    if (stat(src, &st) != 0) {
        ESP_LOGW(kTag, "Active params stat failed: %s errno=%d", src, errno);
        s_current_params_file[0] = '\0';
        s_current_params_epoch = 0U;
        return false;
    }
    if (st.st_size <= 0) {
        s_current_params_file[0] = '\0';
        s_current_params_epoch = 0U;
        return true;
    }

    char sealed[kMaxFilenameLen]{};
    if (!MakeUniquePendingFilename("params", GetCurrentEpoch(), sealed, sizeof(sealed))) {
        return false;
    }
    if (!BuildPendingPath(sealed, dst, sizeof(dst))) {
        return false;
    }

    if (!MoveFile(src, dst)) {
        return false;
    }
    ESP_LOGI(kTag, "Sealed params file: %s -> %s", s_current_params_file, sealed);
    s_current_params_file[0] = '\0';
    s_current_params_epoch = 0U;
    return true;
}

bool RotateParameterFile() noexcept {
    OutboxLock lock;
    if (!lock.locked()) {
        return false;
    }
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

    OutboxLock lock;
    if (!lock.locked()) {
        return false;
    }

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
        bool is_failure = false;
        if (!ClassifyPendingFilename(name, is_failure)) {
            continue;
        }

        std::strncpy(out_entries[out_count].filename.data(), name, kMaxFilenameLen - 1U);
        out_entries[out_count].filename[kMaxFilenameLen - 1U] = '\0';
        out_entries[out_count].is_failure = is_failure;
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
    OutboxLock lock;
    if (!lock.locked()) {
        return false;
    }
    if (!BuildPendingPath(filename, src, sizeof(src))) {
        return false;
    }
    if (!MakeUniqueSentPath(filename, dst, sizeof(dst))) {
        return false;
    }

    return MoveFile(src, dst);
}

bool ClassifyPendingFilenameForUpload(const char* filename, bool& out_is_failure) noexcept {
    return ClassifyPendingFilename(filename, out_is_failure);
}

bool FormatUploadFilenameForTest(const char* prefix,
                                 std::uint32_t epoch,
                                 std::uint32_t boot_id,
                                 std::uint32_t sequence,
                                 char* out_name,
                                 std::size_t out_len) noexcept {
    return FormatUploadFilename(prefix, epoch, boot_id, sequence, out_name, out_len);
}

bool FormatSentCollisionFilenameForTest(const char* filename,
                                        std::uint32_t sequence,
                                        char* out_name,
                                        std::size_t out_len) noexcept {
    return FormatSentCollisionFilename(filename, sequence, out_name, out_len);
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
