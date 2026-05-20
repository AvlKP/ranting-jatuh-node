#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "monitor.hpp"

namespace logger {

class Logger {
public:
    struct Config {
        const char* sd_mount_point{nullptr};
    };

    [[nodiscard]] bool Init(const Config& config) noexcept;
    void HandleMonitorEvent(const monitor::MonitorResult& result) noexcept;
    void HandleFailureEvent(const monitor::FailureResult& result) noexcept;
    static void MonitorCallback(void* ctx, const monitor::MonitorResult& result) noexcept;
    static void FailureCallback(void* ctx, const monitor::FailureResult& result) noexcept;
    // Superloop note: SD/MQTT IO can block and add jitter; call Poll often.
    // RTOS task+queue isolates IO, keeps monitor loop deterministic, and avoids long stalls.
    void Poll() noexcept;
    [[nodiscard]] bool VerifyMqttPublish(const char* topic, const char* payload) noexcept;
    [[nodiscard]] bool HasMonitorResult() const noexcept;

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

    [[nodiscard]] bool Enqueue(const Event& event) noexcept;
    [[nodiscard]] bool Dequeue(Event& event) noexcept;

    static constexpr std::size_t kQueueDepth = 16U;
    std::array<Event, kQueueDepth> queue_{};
    std::size_t queue_head_{0U};
    std::size_t queue_count_{0U};

    std::uint32_t dropped_events_{0U};
    std::uint32_t dropped_parameters_{0U};
    std::uint32_t dropped_failures_{0U};
    std::uint64_t next_publish_us_{0U};
    const char* sd_mount_point_{nullptr};
    bool has_monitor_result_{false};
};

} // namespace logger
