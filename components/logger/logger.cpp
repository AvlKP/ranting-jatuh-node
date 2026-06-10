#include "logger.hpp"
#include "logger_internal.hpp"
#include "outbox.hpp"
#include "network_task.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

namespace logger {

namespace {

constexpr std::uint32_t kMinValidEpoch = 1672531200U;
static const char* kTag = "LOGGER";
constexpr std::size_t kTaskStackSize = 6144U;
static_assert(kTaskStackSize >= 2048U, "Logger task stack too small for ESP-IDF task minimum.");
static_assert(kTaskStackSize <= 16384U, "Logger task stack exceeds bounded RAM budget.");
constexpr UBaseType_t kTaskPriority = 4U;
constexpr BaseType_t kTaskCore = 0;

void LoggerTaskEntry(void* arg) noexcept {
    auto* self = static_cast<Logger*>(arg);
    self->TaskLoop();
}

} // namespace

const char* FailureEventName(monitor::FailureEvent event) noexcept {
    switch (event) {
        case monitor::FailureEvent::FreeFall:
            return "free_fall";
        case monitor::FailureEvent::AcousticEmission:
            return "acoustic_emission";
        default:
            return "unknown";
    }
}

bool BuildTimeInfo(TimeInfo& out_time) noexcept {
    std::time_t now = 0;
    if (std::time(&now) == static_cast<std::time_t>(-1)) {
        out_time.valid = false;
        out_time.unix_time = 0;
        return false;
    }

    if (static_cast<std::uint32_t>(now) < kMinValidEpoch) {
        out_time.valid = false;
        out_time.unix_time = 0;
        return false;
    }

    std::tm timeinfo{};
    if (localtime_r(&now, &timeinfo) == nullptr) {
        out_time.valid = false;
        out_time.unix_time = 0;
        return false;
    }

    const int len = std::snprintf(out_time.date_yyyymmdd.data(),
                                  out_time.date_yyyymmdd.size(),
                                  "%04d%02d%02d",
                                  timeinfo.tm_year + 1900,
                                  timeinfo.tm_mon + 1,
                                  timeinfo.tm_mday);
    if (len <= 0 || static_cast<std::size_t>(len) >= out_time.date_yyyymmdd.size()) {
        out_time.valid = false;
        out_time.unix_time = 0;
        return false;
    }

    out_time.valid = true;
    out_time.unix_time = static_cast<std::int64_t>(now);
    return true;
}

bool FormatParameterCsv(const monitor::MonitorResult& result,
                        const TimeInfo& time_info,
                        CsvLine& line) noexcept {
    const std::int64_t unix_time = time_info.valid ? time_info.unix_time : 0;

    const char* state_str = "IDLE";
    if (result.state == monitor::NodeState::DISTURBED) {
        state_str = "DISTURBED";
    }

    const int len = std::snprintf(line.buffer.data(),
                                  line.buffer.size(),
                                  "%lld,%llu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.4f,%.4f,%.3f,%.3f,%.3f,%s,%s,%u\n",
                                  static_cast<long long>(unix_time),
                                  static_cast<unsigned long long>(time_info.timestamp_us),
                                  result.roll_mean,
                                  result.pitch_mean,
                                  result.roll_variance,
                                  result.pitch_variance,
                                  result.roll_sway_pp_max,
                                  result.roll_sway_pp_mean,
                                  result.pitch_sway_pp_max,
                                  result.pitch_sway_pp_mean,
                                  result.roll_damping_ratio,
                                  result.pitch_damping_ratio,
                                  result.natural_freq_hz,
                                  result.natural_freq_roll_hz,
                                  result.natural_freq_pitch_hz,
                                  result.damping_confidence.data(),
                                  state_str,
                                  static_cast<unsigned>(result.sample_count));
    if (len <= 0 || static_cast<std::size_t>(len) >= line.buffer.size()) {
        return false;
    }

    line.length = static_cast<std::uint16_t>(len);
    return true;
}

bool FormatParameterJson(const monitor::MonitorResult& result,
                         const TimeInfo& time_info,
                         CsvLine& line) noexcept {
    const std::int64_t unix_time = time_info.valid ? time_info.unix_time : 0;

    const char* state_str = "IDLE";
    if (result.state == monitor::NodeState::DISTURBED) {
        state_str = "DISTURBED";
    }

    const int len = std::snprintf(line.buffer.data(),
                                  line.buffer.size(),
                                  "{\"unix_time\":%lld,\"timestamp_us\":%llu,"
                                  "\"roll_mean\":%.3f,\"pitch_mean\":%.3f,"
                                  "\"roll_variance\":%.3f,\"pitch_variance\":%.3f,"
                                  "\"roll_sway_pp_max\":%.3f,\"roll_sway_pp_mean\":%.3f,"
                                  "\"pitch_sway_pp_max\":%.3f,\"pitch_sway_pp_mean\":%.3f,"
                                  "\"roll_damping_ratio\":%.4f,\"pitch_damping_ratio\":%.4f,"
                                  "\"natural_freq_hz\":%.3f,\"natural_freq_roll_hz\":%.3f,"
                                  "\"natural_freq_pitch_hz\":%.3f,\"damping_confidence\":\"%s\",\"state\":\"%s\","
                                  "\"sample_count\":%u}\n",
                                  static_cast<long long>(unix_time),
                                  static_cast<unsigned long long>(time_info.timestamp_us),
                                  result.roll_mean,
                                  result.pitch_mean,
                                  result.roll_variance,
                                  result.pitch_variance,
                                  result.roll_sway_pp_max,
                                  result.roll_sway_pp_mean,
                                  result.pitch_sway_pp_max,
                                  result.pitch_sway_pp_mean,
                                  result.roll_damping_ratio,
                                  result.pitch_damping_ratio,
                                  result.natural_freq_hz,
                                  result.natural_freq_roll_hz,
                                  result.natural_freq_pitch_hz,
                                  result.damping_confidence.data(),
                                  state_str,
                                  static_cast<unsigned>(result.sample_count));
    if (len <= 0 || static_cast<std::size_t>(len) >= line.buffer.size()) {
        return false;
    }

    line.length = static_cast<std::uint16_t>(len);
    return true;
}

bool FormatFailureCsv(const monitor::FailureResult& result,
                      const TimeInfo& time_info,
                      CsvLine& line) noexcept {
    const std::int64_t unix_time = time_info.valid ? time_info.unix_time : 0;
    const char* event_name = FailureEventName(result.event);
    const int len = std::snprintf(line.buffer.data(),
                                  line.buffer.size(),
                                  "%lld,%llu,%s\n",
                                  static_cast<long long>(unix_time),
                                  static_cast<unsigned long long>(time_info.timestamp_us),
                                  event_name);
    if (len <= 0 || static_cast<std::size_t>(len) >= line.buffer.size()) {
        return false;
    }

    line.length = static_cast<std::uint16_t>(len);
    return true;
}

bool FormatFailureJson(const monitor::FailureResult& result,
                       const TimeInfo& time_info,
                       CsvLine& line) noexcept {
    const std::int64_t unix_time = time_info.valid ? time_info.unix_time : 0;
    const char* event_name = FailureEventName(result.event);
    const int len = std::snprintf(line.buffer.data(),
                                  line.buffer.size(),
                                  "{\"unix_time\":%lld,\"timestamp_us\":%llu,\"event\":\"%s\"}\n",
                                  static_cast<long long>(unix_time),
                                  static_cast<unsigned long long>(time_info.timestamp_us),
                                  event_name);
    if (len <= 0 || static_cast<std::size_t>(len) >= line.buffer.size()) {
        return false;
    }

    line.length = static_cast<std::uint16_t>(len);
    return true;
}

bool Logger::Init(const Config& config) noexcept {
    if (config.sd_mount_point == nullptr || std::strlen(config.sd_mount_point) == 0U) {
        ESP_LOGE(kTag, "SD mount point not set");
        return false;
    }

    sd_mount_point_ = config.sd_mount_point;
    storage::SetMountPoint(sd_mount_point_);

    // outbox and network task are initialized externally in main.cpp

    queue_handle_ = xQueueCreate(kQueueDepth, sizeof(Event));
    if (queue_handle_ == nullptr) {
        ESP_LOGE(kTag, "Failed to create logger queue");
        return false;
    }

    esp_err_t err = esp_event_handler_register(
        monitor::MONITOR_EVENT_BASE,
        monitor::MONITOR_EVENT_RESULT,
        &Logger::EventHandler,
        this);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to register MONITOR_EVENT_RESULT handler: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_event_handler_register(
        monitor::MONITOR_EVENT_BASE,
        monitor::MONITOR_EVENT_FAILURE,
        &Logger::EventHandler,
        this);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to register MONITOR_EVENT_FAILURE handler: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}

// ============================================================================
// ESP Event Handler — called from system event task, must be non-blocking.
// Copies event data into FreeRTOS queue with zero wait. Tracks drops per
// event type for backpressure diagnostics.
// ============================================================================
void Logger::EventHandler(void* handler_args,
                           esp_event_base_t base,
                           std::int32_t id,
                           void* event_data) noexcept {
    auto* self = static_cast<Logger*>(handler_args);
    if (self == nullptr || self->queue_handle_ == nullptr) {
        return;
    }

    Event event{};
    if (id == monitor::MONITOR_EVENT_RESULT) {
        event.type = EventType::Parameters;
        event.monitor = *static_cast<const monitor::MonitorResult*>(event_data);
        self->has_monitor_result_ = true;
    } else if (id == monitor::MONITOR_EVENT_FAILURE) {
        event.type = EventType::Failure;
        event.failure = *static_cast<const monitor::FailureResult*>(event_data);
    } else {
        return;
    }

    if (xQueueSend(self->queue_handle_, &event, 0) != pdTRUE) {
        ++self->dropped_events_;
        if (event.type == EventType::Failure) {
            ++self->dropped_failures_;
        } else {
            ++self->dropped_parameters_;
        }
        ESP_LOGW(kTag, "Event queue full, dropped");
    }
}

bool Logger::Start() noexcept {
    const BaseType_t ret = xTaskCreatePinnedToCore(
        LoggerTaskEntry,
        "logger_task",
        kTaskStackSize,
        this,
        kTaskPriority,
        &task_handle_,
        kTaskCore);
    if (ret != pdPASS) {
        const std::uint32_t free_internal = static_cast<std::uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        const std::uint32_t largest_block = static_cast<std::uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        ESP_LOGE(kTag, "Failed to create logger_task (stack=%u free_internal=%lu largest_block=%lu)",
                 static_cast<unsigned>(kTaskStackSize),
                 static_cast<unsigned long>(free_internal),
                 static_cast<unsigned long>(largest_block));
        return false;
    }
    ESP_LOGI(kTag, "logger_task started on core %d, priority %u", kTaskCore, kTaskPriority);
    return true;
}

// ============================================================================
// Logger Task Loop — runs on core 0, priority 4.
// Blocks on queue receive (100ms tick), then:
//   - Parameters: format CSV + JSON, write CSV to SD, append JSON to outbox, notify network task
//   - Failures: write CSV to SD first, then append JSON to outbox, notify network task
// No direct MQTT or WiFi operations in this task.
// ============================================================================
void Logger::TaskLoop() noexcept {
    while (true) {
        Event event{};
        if (xQueueReceive(queue_handle_, &event, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (event.type == EventType::Parameters) {
                TimeInfo time_info{};
                time_info.timestamp_us = event.monitor.timestamp_us;
                static_cast<void>(BuildTimeInfo(time_info));

                CsvLine line_csv{};
                CsvLine line_json{};
                bool csv_ok = FormatParameterCsv(event.monitor, time_info, line_csv);
                bool json_ok = FormatParameterJson(event.monitor, time_info, line_json);
                if (!csv_ok || !json_ok) {
                    ESP_LOGE(kTag, "Format parameter CSV or JSON failed");
                } else {
                    if (!storage::AppendParameter(time_info, line_csv)) {
                        ESP_LOGE(kTag, "Parameter storage write failed");
                    }
#if CONFIG_LOGGER_SERIAL_OUTPUT
                    ESP_LOGI(kTag, "param_csv=%.*s",
                             static_cast<int>(line_csv.length),
                             line_csv.buffer.data());
#endif
                    if (!outbox::AppendParameter(line_json.buffer.data())) {
                        ESP_LOGE(kTag, "Outbox parameter append failed");
                    } else {
                        network_task::EnqueueNotify();
                    }
                }
            } else {
                TimeInfo time_info{};
                time_info.timestamp_us = event.failure.timestamp_us;
                static_cast<void>(BuildTimeInfo(time_info));

                CsvLine line_csv{};
                CsvLine line_json{};
                bool csv_ok = FormatFailureCsv(event.failure, time_info, line_csv);
                bool json_ok = FormatFailureJson(event.failure, time_info, line_json);
                if (!csv_ok || !json_ok) {
                    ESP_LOGE(kTag, "Format failure CSV or JSON failed");
                } else {
                    // SD write first, always
                    if (!storage::AppendFailure(time_info, line_csv)) {
                        ESP_LOGE(kTag, "Failure storage write failed");
                    }
#if CONFIG_LOGGER_SERIAL_OUTPUT
                    ESP_LOGI(kTag, "failure_csv=%.*s",
                             static_cast<int>(line_csv.length),
                             line_csv.buffer.data());
#endif
                    // Outbox write is best-effort, async
                    if (!outbox::AppendFailure(line_json.buffer.data())) {
                        ESP_LOGE(kTag, "Outbox failure append failed");
                    } else {
                        network_task::EnqueueNotify();
                    }
                }
            }
        }
    }
}

bool Logger::VerifyMqttPublish(const char* /*topic*/, const char* /*payload*/) noexcept {
    // MQTT publishing is now handled by network_task.
    // Startup verification is implicit: network task publishes any pending outbox files on first connect.
    network_task::EnqueueNotify();
    return true;
}

bool Logger::HasMonitorResult() const noexcept {
    return has_monitor_result_;
}

} // namespace logger
