/// @file logger.hpp
/// @brief MicroSD CSV logger and MQTT publisher for monitor output.
/// @details Receives MonitorResult and FailureResult via ESP event loop, buffers
/// them in a FreeRTOS queue, writes CSV lines to microSD (FAT FS), and publishes
/// JSON/CSV payloads to MQTT. Runs on its own FreeRTOS task (core 0, priority 4).
/// Supports batched parameter publishing for deployment and per-event publishing for debug.
/// @ingroup logger

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_event.h"

#include "monitor.hpp"
#include "monitor_events.hpp"

namespace logger {

/// @brief Logs monitor output to microSD and publishes to MQTT.
class Logger {
public:
    /// @brief Logger configuration.
    struct Config {
        const char* sd_mount_point{nullptr}; ///< VFS mount point for SD card (e.g., "/sdcard").
    };

    /// @brief Initialize the ESP event handlers and FreeRTOS queue.
    /// @param config Logger configuration with SD mount point.
    /// @return true if event handlers registered and queue created.
    [[nodiscard]] bool Init(const Config& config) noexcept;
    /// @brief Create the FreeRTOS logger task.
    /// @return true if task created successfully.
    [[nodiscard]] bool Start() noexcept;
    /// @brief Publish a test message to MQTT (for startup verification).
    /// @param topic MQTT topic string.
    /// @param payload MQTT payload string.
    /// @return true if published successfully.
    [[nodiscard]] bool VerifyMqttPublish(const char* topic, const char* payload) noexcept;
    /// @brief Check if at least one MonitorResult has been received.
    [[nodiscard]] bool HasMonitorResult() const noexcept;
    /// @brief FreeRTOS task handle of the logger task.
    [[nodiscard]] TaskHandle_t GetTaskHandle() const noexcept { return task_handle_; }
    /// @brief Count of dropped events (queue full).
    [[nodiscard]] std::uint32_t DroppedEvents() const noexcept { return dropped_events_; }
    /// @brief Count of dropped parameter publish events.
    [[nodiscard]] std::uint32_t DroppedParameters() const noexcept { return dropped_parameters_; }
    /// @brief Count of dropped failure publish events.
    [[nodiscard]] std::uint32_t DroppedFailures() const noexcept { return dropped_failures_; }

    /// @brief ESP event loop callback. Copies event data into the FreeRTOS queue.
    /// Called from the system event task; non-blocking (zero wait on queue send).
    static void EventHandler(void* handler_args,
                              esp_event_base_t base,
                              std::int32_t id,
                              void* event_data) noexcept;
    /// @brief FreeRTOS task entry point. Loops on queue receive, writes CSV to SD,
    /// and publishes MQTT when connected.
    void TaskLoop() noexcept;

private:
    enum class EventType : std::uint8_t {
        Parameters = 0U,
        Failure = 1U
    };

    struct Event {
        EventType type{EventType::Parameters};
        monitor::MonitorResult monitor{};
        monitor::FailureResult failure{};
    };

    static constexpr std::size_t kQueueDepth = 16U;
    static_assert(kQueueDepth <= 32U, "Logger queue depth exceeds bounded RAM budget.");
    QueueHandle_t queue_handle_{nullptr};
    TaskHandle_t task_handle_{nullptr};

    std::uint32_t dropped_events_{0U};
    std::uint32_t dropped_parameters_{0U};
    std::uint32_t dropped_failures_{0U};
    std::uint64_t next_publish_us_{0U};
    const char* sd_mount_point_{nullptr};
    bool has_monitor_result_{false};
};

} // namespace logger
