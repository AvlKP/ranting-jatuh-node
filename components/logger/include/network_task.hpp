/// @file network_task.hpp
/// @brief Dedicated FreeRTOS task for WiFi/MQTT publish operations.
/// @details Runs independently from logger task. Scans SD outbox for pending
/// files, connects WiFi/MQTT, publishes contents, manages backoff.
/// Logger task enqueues notifications when new data is available.
/// @ingroup logger

#pragma once

#include <cstddef>

#include "outbox.hpp"

namespace logger::network_task {

/// @brief Initialize the network task context.
/// @param mount_point SD card VFS mount point for outbox access.
/// @return true if initialization succeeded.
[[nodiscard]] bool Init(const char* mount_point) noexcept;

/// @brief Start the FreeRTOS network task (core 0, priority 3).
/// @return true if task created.
[[nodiscard]] bool Start() noexcept;

/// @brief Notify the network task that new outbox data is available.
/// Lightweight — can be called from any task.
void EnqueueNotify() noexcept;

[[nodiscard]] std::size_t SelectPublishEntriesForTest(const outbox::FileEntry* entries,
                                                      std::size_t file_count,
                                                      bool params_due,
                                                      outbox::FileEntry* out_entries,
                                                      std::size_t out_capacity) noexcept;

} // namespace logger::network_task
