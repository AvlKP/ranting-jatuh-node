#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "monitor.hpp"
#include "logger.hpp"

namespace verify {

void LogStackHighWatermark(const char* stage);
bool VerifySdStorage();
bool VerifyMqtt(logger::Logger& logger);
bool VerifyMonitorOutput(logger::Logger& logger);

} // namespace verify
