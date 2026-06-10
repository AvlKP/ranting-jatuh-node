/// @file monitor_events.hpp
/// @brief ESP event loop declarations for monitor output.
/// @details Defines MONITOR_EVENT_BASE and MonitorEventId enum for publishing
/// MonitorResult (MONITOR_EVENT_RESULT) and FailureResult (MONITOR_EVENT_FAILURE)
/// to the default ESP event loop. Logger and dashboard subscribe to these events.
/// @ingroup monitor

#pragma once

#include <cstdint>
#include "esp_event.h"

namespace monitor {

ESP_EVENT_DECLARE_BASE(MONITOR_EVENT_BASE);

/// @brief Monitor event types published via ESP event loop.
enum MonitorEventId : int32_t {
    MONITOR_EVENT_RESULT = 0,   ///< MonitorResult payload (parameters, state).
    MONITOR_EVENT_FAILURE = 1   ///< FailureResult payload (free-fall, AE).
};

} // namespace monitor
