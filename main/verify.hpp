#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "monitor.hpp"
#include "logger.hpp"

namespace verify {

void LogStackHighWatermark(const char* stage);
void LogTaskStackHighWatermark(const char* task_name, TaskHandle_t task);
void LogRuntimeDiagnostics(const char* stage,
                           TaskHandle_t monitor_task,
                           TaskHandle_t logger_task,
                           TaskHandle_t ae_spectral_task = nullptr);
bool VerifySdStorage();
bool VerifyMqtt(logger::Logger& logger);
bool VerifyMonitorOutput(logger::Logger& logger);

} // namespace verify
