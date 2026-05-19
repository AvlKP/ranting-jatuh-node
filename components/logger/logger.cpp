#include "logger.hpp"
#include "logger_internal.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>

#include "sdkconfig.h"

namespace logger {

namespace {

constexpr std::size_t kPendingParamsMax = 32U;
constexpr TickType_t kLoggerTaskPriority = tskIDLE_PRIORITY + 1;
constexpr std::uint32_t kMinValidEpoch = 1672531200U;
constexpr std::uint32_t kPublishPeriodSec =
    static_cast<std::uint32_t>(CONFIG_LOGGER_WIFI_PERIOD_HOURS) * 3600U;

struct PendingParamsBuffer {
    std::array<CsvLine, kPendingParamsMax> lines{};
    std::size_t head{0U};
    std::size_t count{0U};
};

PendingParamsBuffer g_pending_params{};

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
    const int len = std::snprintf(line.buffer.data(),
                                  line.buffer.size(),
                                  "%lld,%llu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.4f,%.4f,%.3f,%u\n",
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

bool Logger::Init() noexcept {
    if (queue_ != nullptr) {
        return true;
    }

    queue_ = xQueueCreateStatic(static_cast<UBaseType_t>(kQueueDepth),
                                sizeof(Event),
                                queue_storage_.data(),
                                &queue_buffer_);
    return queue_ != nullptr;
}

bool Logger::Start() noexcept {
    if (started_) {
        return true;
    }

    if (queue_ == nullptr && !Init()) {
        std::printf("logger: queue init failed\n");
        return false;
    }

    TaskHandle_t task = xTaskCreateStatic(&Logger::TaskThunk,
                                          "logger",
                                          static_cast<uint32_t>(kTaskStackWords),
                                          this,
                                          kLoggerTaskPriority,
                                          task_stack_.data(),
                                          &task_buffer_);
    if (task == nullptr) {
        std::printf("logger: task create failed\n");
        return false;
    }

    started_ = true;
    return true;
}

bool Logger::Enqueue(const Event& event) noexcept {
    if (queue_ == nullptr) {
        return false;
    }
    return xQueueSend(queue_, &event, 0) == pdTRUE;
}

void Logger::HandleMonitorEvent(const monitor::MonitorResult& result) noexcept {
    Event event{};
    event.type = EventType::Parameters;
    event.monitor = result;
    if (!Enqueue(event)) {
        ++dropped_events_;
        ++dropped_parameters_;
        std::printf("logger: parameter event dropped\n");
    }
}

void Logger::HandleFailureEvent(const monitor::FailureResult& result) noexcept {
    Event event{};
    event.type = EventType::Failure;
    event.failure = result;
    if (!Enqueue(event)) {
        ++dropped_events_;
        ++dropped_failures_;
        std::printf("logger: failure event dropped\n");
    }
}

void Logger::MonitorCallback(void* ctx, const monitor::MonitorResult& result) noexcept {
    if (ctx == nullptr) {
        return;
    }

    static_cast<Logger*>(ctx)->HandleMonitorEvent(result);
}

void Logger::FailureCallback(void* ctx, const monitor::FailureResult& result) noexcept {
    if (ctx == nullptr) {
        return;
    }

    static_cast<Logger*>(ctx)->HandleFailureEvent(result);
}

void Logger::TaskThunk(void* ctx) {
    if (ctx == nullptr) {
        vTaskDelete(nullptr);
        return;
    }

    static_cast<Logger*>(ctx)->TaskLoop();
}

void Logger::TaskLoop() noexcept {
    if (!storage::Init()) {
        std::printf("logger: storage init failed\n");
    }
    if (!mqtt::Init()) {
        std::printf("logger: mqtt init failed\n");
    }

    const TickType_t publish_period_ticks =
        pdMS_TO_TICKS(static_cast<std::uint64_t>(kPublishPeriodSec) * 1000ULL);
    TickType_t next_publish = xTaskGetTickCount() + publish_period_ticks;

    while (true) {
        Event event{};
        const TickType_t now = xTaskGetTickCount();
        const TickType_t wait = (now < next_publish) ? (next_publish - now) : 0U;
        const bool got_event = xQueueReceive(queue_, &event, wait) == pdTRUE;

        if (got_event) {
            if (event.type == EventType::Parameters) {
                TimeInfo time_info{};
                time_info.timestamp_us = event.monitor.timestamp_us;
                static_cast<void>(BuildTimeInfo(time_info));

                CsvLine line{};
                if (!FormatParameterCsv(event.monitor, time_info, line)) {
                    std::printf("logger: format parameter csv failed\n");
                } else {
                    if (!storage::AppendParameter(time_info, line)) {
                        std::printf("logger: parameter storage write failed\n");
                    }
#if CONFIG_LOGGER_SERIAL_OUTPUT
                    std::printf("%s", line.buffer.data());
#endif
                    if (g_pending_params.count == kPendingParamsMax) {
                        g_pending_params.head = (g_pending_params.head + 1U) % kPendingParamsMax;
                        --g_pending_params.count;
                        std::printf("logger: pending parameter buffer full, drop oldest\n");
                    }

                    const std::size_t tail = (g_pending_params.head + g_pending_params.count) %
                        kPendingParamsMax;
                    g_pending_params.lines[tail] = line;
                    ++g_pending_params.count;
                }
            } else {
                TimeInfo time_info{};
                time_info.timestamp_us = event.failure.timestamp_us;
                static_cast<void>(BuildTimeInfo(time_info));

                CsvLine line{};
                if (!FormatFailureCsv(event.failure, time_info, line)) {
                    std::printf("logger: format failure csv failed\n");
                } else {
                    if (!storage::AppendFailure(time_info, line)) {
                        std::printf("logger: failure storage write failed\n");
                    }
#if CONFIG_LOGGER_SERIAL_OUTPUT
                    std::printf("%s", line.buffer.data());
#endif
                    if (!mqtt::PublishFailure(line)) {
                        std::printf("logger: failure mqtt publish failed\n");
                    }
                }
            }
        }

        if (xTaskGetTickCount() >= next_publish) {
            if (g_pending_params.count > 0U) {
                std::array<CsvLine, kPendingParamsMax> batch{};
                for (std::size_t i = 0U; i < g_pending_params.count; ++i) {
                    const std::size_t idx = (g_pending_params.head + i) % kPendingParamsMax;
                    batch[i] = g_pending_params.lines[idx];
                }

                if (mqtt::PublishParameters(batch.data(), g_pending_params.count)) {
                    g_pending_params.head = 0U;
                    g_pending_params.count = 0U;
                } else {
                    std::printf("logger: parameter mqtt publish failed\n");
                }
            }

            next_publish = xTaskGetTickCount() + publish_period_ticks;
        }
    }
}

} // namespace logger
