#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "monitor.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace logger {

class Logger {
public:
    [[nodiscard]] bool Init() noexcept;
    [[nodiscard]] bool Start() noexcept;
    void HandleMonitorEvent(const monitor::MonitorResult& result) noexcept;
    void HandleFailureEvent(const monitor::FailureResult& result) noexcept;
    static void MonitorCallback(void* ctx, const monitor::MonitorResult& result) noexcept;
    static void FailureCallback(void* ctx, const monitor::FailureResult& result) noexcept;

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

    static void TaskThunk(void* ctx);
    void TaskLoop() noexcept;
    [[nodiscard]] bool Enqueue(const Event& event) noexcept;

    static constexpr std::size_t kQueueDepth = 16U;
    static constexpr std::size_t kTaskStackBytes = 4096U;
    static constexpr std::size_t kTaskStackWords = kTaskStackBytes / sizeof(StackType_t);

    StaticQueue_t queue_buffer_{};
    QueueHandle_t queue_{nullptr};
    std::array<std::uint8_t, kQueueDepth * sizeof(Event)> queue_storage_{};

    StaticTask_t task_buffer_{};
    std::array<StackType_t, kTaskStackWords> task_stack_{};

    std::uint32_t dropped_events_{0U};
    std::uint32_t dropped_parameters_{0U};
    std::uint32_t dropped_failures_{0U};
    bool started_{false};
};

} // namespace logger
