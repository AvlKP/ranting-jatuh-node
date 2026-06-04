#pragma once

#include <cstdint>
#include "esp_event.h"

namespace monitor {

// Declare the event base for the monitor component events
ESP_EVENT_DECLARE_BASE(MONITOR_EVENT_BASE);

enum MonitorEventId : int32_t {
    MONITOR_EVENT_RESULT = 0,
    MONITOR_EVENT_FAILURE = 1,
    MONITOR_EVENT_STREAM_SAMPLE = 2
};

} // namespace monitor
