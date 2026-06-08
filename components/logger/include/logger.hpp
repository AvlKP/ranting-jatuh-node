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

class Logger {
public:
    struct Config {
        const char* sd_mount_point{nullptr};
    };

    [[nodiscard]] bool Init(const Config& config) noexcept;
    [[nodiscard]] bool Start() noexcept;
    [[nodiscard]] bool VerifyMqttPublish(const char* topic, const char* payload) noexcept;
    [[nodiscard]] bool HasMonitorResult() const noexcept;
    [[nodiscard]] TaskHandle_t GetTaskHandle() const noexcept { return task_handle_; }
    [[nodiscard]] std::uint32_t DroppedEvents() const noexcept { return dropped_events_; }
    [[nodiscard]] std::uint32_t DroppedParameters() const noexcept { return dropped_parameters_; }
    [[nodiscard]] std::uint32_t DroppedFailures() const noexcept { return dropped_failures_; }

    static void EventHandler(void* handler_args,
                              esp_event_base_t base,
                              std::int32_t id,
                              void* event_data) noexcept;
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
