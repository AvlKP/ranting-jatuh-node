/// @file outbox.hpp
/// @brief SD-backed upload queue for MQTT payloads.
/// @details Writes JSONL lines to pending files on SD card. Network task scans
/// pending directory, publishes contents via MQTT, moves files to sent/.
/// Survives reboot — pending files persist on SD.
/// @ingroup logger

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace logger::outbox {

constexpr std::size_t kMaxPendingFiles = 32U;
constexpr std::size_t kMaxFilenameLen = 64U;
constexpr std::size_t kPathMax = 128U;

struct FileEntry {
    std::array<char, kMaxFilenameLen> filename{};
    bool is_failure{false};
};

[[nodiscard]] bool ClassifyPendingFilenameForUpload(const char* filename,
                                                    bool& out_is_failure) noexcept;
[[nodiscard]] bool FormatUploadFilenameForTest(const char* prefix,
                                               std::uint32_t epoch,
                                               std::uint32_t boot_id,
                                               std::uint32_t sequence,
                                               char* out_name,
                                               std::size_t out_len) noexcept;
[[nodiscard]] bool FormatSentCollisionFilenameForTest(const char* filename,
                                                      std::uint32_t sequence,
                                                      char* out_name,
                                                      std::size_t out_len) noexcept;

/// @brief Initialize outbox directories on SD card.
/// Creates outbox/pending/ and outbox/sent/ if they don't exist.
/// Scans pending/ for files from prior boot.
/// @param mount_point SD card VFS mount point (e.g., "/sdcard").
/// @return true if directories ensured.
[[nodiscard]] bool Init(const char* mount_point) noexcept;

/// @brief Append a parameter JSON line to the active non-uploadable file.
/// Creates a new params_active_<boot>_<seq>.jsonl file if needed.
/// @param json_line Null-terminated JSON line to append.
/// @return true if append succeeded.
[[nodiscard]] bool AppendParameter(const char* json_line) noexcept;

/// @brief Append a failure JSON line to an individual pending file.
/// Creates a new failure_<epoch>.jsonl file.
/// @param json_line Null-terminated JSON line to write.
/// @return true if write succeeded.
[[nodiscard]] bool AppendFailure(const char* json_line) noexcept;

/// @brief Seal the active parameter file into an immutable uploadable file.
/// @return true if no active file existed or sealing succeeded.
[[nodiscard]] bool SealParameterFile() noexcept;

/// @brief Drop the current active parameter file name so next append creates one.
/// @return true if rotation state reset succeeded.
[[nodiscard]] bool RotateParameterFile() noexcept;

/// @brief Get list of pending files sorted by priority.
/// Failure files sorted before parameter files.
/// @param[out] out_entries Array to fill with file entries.
/// @param[out] out_count Number of files found.
/// @return true if scan succeeded.
[[nodiscard]] bool GetPendingFiles(FileEntry* out_entries, std::size_t& out_count) noexcept;

/// @brief Mark a pending file as sent (move to sent/).
/// @param filename Just the filename (not full path).
/// @return true if move succeeded.
[[nodiscard]] bool MarkSent(const char* filename) noexcept;

/// @brief Prune old files from sent/ directory.
/// Deletes oldest files when count exceeds max_keep.
/// @param max_keep Maximum number of sent files to retain.
/// @return true if pruning succeeded (or no prune needed).
[[nodiscard]] bool PruneSent(std::size_t max_keep) noexcept;

} // namespace logger::outbox
