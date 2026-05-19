#pragma once

#include "monitor.hpp"

namespace logger {

class Logger {
public:
    void HandleMonitorEvent(const monitor::MonitorResult& result) noexcept;
    static void MonitorCallback(void* ctx, const monitor::MonitorResult& result) noexcept;
};

} // namespace logger
