#include "logger.hpp"

#include <cstdio>

namespace logger {

void Logger::HandleMonitorEvent(const monitor::MonitorResult& result) noexcept {
    std::printf("monitor: roll_mean=%.3f pitch_mean=%.3f roll_var=%.3f pitch_var=%.3f freq=%.3fHz samples=%u\n",
                result.roll_mean,
                result.pitch_mean,
                result.roll_variance,
                result.pitch_variance,
                result.natural_freq_hz,
                static_cast<unsigned>(result.sample_count));
}

void Logger::MonitorCallback(void* ctx, const monitor::MonitorResult& result) noexcept {
    if (ctx == nullptr) {
        return;
    }

    static_cast<Logger*>(ctx)->HandleMonitorEvent(result);
}

} // namespace logger
