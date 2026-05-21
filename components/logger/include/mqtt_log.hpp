#pragma once

#include <array>
#include <cstddef>
#include <cstring>
#include <mutex>

namespace logger::mqtt {

constexpr std::size_t kMaxMqttLogLines = 20U;
constexpr std::size_t kMaxMqttLogLineLen = 96U;

class MqttLogBuffer {
public:
    void Log(const char* message) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::strncpy(lines_[write_idx_].data(), message, kMaxMqttLogLineLen - 1U);
        lines_[write_idx_][kMaxMqttLogLineLen - 1U] = '\0';
        
        write_idx_ = (write_idx_ + 1U) % kMaxMqttLogLines;
        if (count_ < kMaxMqttLogLines) {
            ++count_;
        }
    }

    // Safely reads logs in chronological order
    // out_logs must be pre-allocated buffer of size max_lines * kMaxMqttLogLineLen
    void GetLogs(char* out_logs, std::size_t max_lines, std::size_t& out_count) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        
        out_count = (count_ < max_lines) ? count_ : max_lines;
        std::size_t start_idx = (count_ < kMaxMqttLogLines) ? 0U : write_idx_;
        
        for (std::size_t i = 0U; i < out_count; ++i) {
            const std::size_t physical_idx = (start_idx + i) % kMaxMqttLogLines;
            std::strncpy(out_logs + (i * kMaxMqttLogLineLen), lines_[physical_idx].data(), kMaxMqttLogLineLen);
        }
    }

private:
    std::array<std::array<char, kMaxMqttLogLineLen>, kMaxMqttLogLines> lines_{};
    std::size_t write_idx_{0U};
    std::size_t count_{0U};
    std::mutex mutex_;
};

extern MqttLogBuffer g_mqtt_log_buffer;

} // namespace logger::mqtt
