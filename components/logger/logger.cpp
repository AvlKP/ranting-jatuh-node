#include "logger.hpp"
#include "logger_internal.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

namespace logger {

namespace {

constexpr std::size_t kPendingParamsMax = 32U;
constexpr std::uint32_t kMinValidEpoch = 1672531200U;
constexpr std::uint64_t kPublishPeriodUs =
    static_cast<std::uint64_t>(CONFIG_LOGGER_WIFI_PERIOD_HOURS) * 60ULL * 1000000ULL;

static const char* kTag = "LOGGER";

struct PendingParamsBuffer {
    std::array<CsvLine, kPendingParamsMax> lines{};
    std::size_t head{0U};
    std::size_t count{0U};
};

PendingParamsBuffer g_pending_params{};
std::array<CsvLine, kPendingParamsMax> g_publish_batch{};

void AppendPendingParameter(const CsvLine& line) {
    if (g_pending_params.count == kPendingParamsMax) {
        g_pending_params.head = (g_pending_params.head + 1U) % kPendingParamsMax;
        --g_pending_params.count;
        ESP_LOGW(kTag, "Pending parameter buffer full, drop oldest");
    }

    const std::size_t tail = (g_pending_params.head + g_pending_params.count) % kPendingParamsMax;
    g_pending_params.lines[tail] = line;
    ++g_pending_params.count;
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

bool Logger::Init(const Config& config) noexcept {
    if (config.sd_mount_point == nullptr || std::strlen(config.sd_mount_point) == 0U) {
        ESP_LOGE(kTag, "SD mount point not set");
        return false;
    }

    sd_mount_point_ = config.sd_mount_point;
    storage::SetMountPoint(sd_mount_point_);

    if (!mqtt::Init()) {
        ESP_LOGE(kTag, "MQTT init failed");
    }

    const std::uint64_t now_us = static_cast<std::uint64_t>(esp_timer_get_time());
    next_publish_us_ = now_us + kPublishPeriodUs;
    return true;
}

bool Logger::Enqueue(const Event& event) noexcept {
    if (kQueueDepth == 0U) {
        return false;
    }

    if (queue_count_ == kQueueDepth) {
        const Event& dropped = queue_[queue_head_];
        ++dropped_events_;
        if (dropped.type == EventType::Failure) {
            ++dropped_failures_;
        } else {
            ++dropped_parameters_;
        }
        ESP_LOGW(kTag, "Event buffer full, drop oldest");
        queue_head_ = (queue_head_ + 1U) % kQueueDepth;
        --queue_count_;
    }

    const std::size_t tail = (queue_head_ + queue_count_) % kQueueDepth;
    queue_[tail] = event;
    ++queue_count_;
    return true;
}

bool Logger::Dequeue(Event& event) noexcept {
    if (queue_count_ == 0U) {
        return false;
    }

    event = queue_[queue_head_];
    queue_head_ = (queue_head_ + 1U) % kQueueDepth;
    --queue_count_;
    return true;
}

void Logger::HandleMonitorEvent(const monitor::MonitorResult& result) noexcept {
    Event event{};
    event.type = EventType::Parameters;
    event.monitor = result;
    has_monitor_result_ = true;
    if (!Enqueue(event)) {
        ++dropped_events_;
        ++dropped_parameters_;
        ESP_LOGW(kTag, "Parameter event dropped");
    }
}

void Logger::HandleFailureEvent(const monitor::FailureResult& result) noexcept {
    Event event{};
    event.type = EventType::Failure;
    event.failure = result;
    if (!Enqueue(event)) {
        ++dropped_events_;
        ++dropped_failures_;
        ESP_LOGW(kTag, "Failure event dropped");
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

void Logger::Poll() noexcept {
    Event event{};
    if (Dequeue(event)) {
        if (event.type == EventType::Parameters) {
            TimeInfo time_info{};
            time_info.timestamp_us = event.monitor.timestamp_us;
            static_cast<void>(BuildTimeInfo(time_info));

            CsvLine line{};
            if (!FormatParameterCsv(event.monitor, time_info, line)) {
                ESP_LOGE(kTag, "Format parameter CSV failed");
            } else {
                if (!storage::AppendParameter(time_info, line)) {
                    ESP_LOGE(kTag, "Parameter storage write failed");
                }
#if CONFIG_LOGGER_SERIAL_OUTPUT
                ESP_LOGI(kTag, "param_csv=%.*s",
                         static_cast<int>(line.length),
                         line.buffer.data());
#endif
                AppendPendingParameter(line);
            }
        } else {
            TimeInfo time_info{};
            time_info.timestamp_us = event.failure.timestamp_us;
            static_cast<void>(BuildTimeInfo(time_info));

            CsvLine line{};
            if (!FormatFailureCsv(event.failure, time_info, line)) {
                ESP_LOGE(kTag, "Format failure CSV failed");
            } else {
                if (!mqtt::PublishFailure(line)) {
                    ESP_LOGE(kTag, "Failure MQTT publish failed");
                }
                if (!storage::AppendFailure(time_info, line)) {
                    ESP_LOGE(kTag, "Failure storage write failed");
                }
#if CONFIG_LOGGER_SERIAL_OUTPUT
                ESP_LOGI(kTag, "failure_csv=%.*s",
                         static_cast<int>(line.length),
                         line.buffer.data());
#endif
            }
        }
    }

    const std::uint64_t now_us = static_cast<std::uint64_t>(esp_timer_get_time());
    if (now_us >= next_publish_us_) {
        if (g_pending_params.count > 0U) {
            for (std::size_t i = 0U; i < g_pending_params.count; ++i) {
                const std::size_t idx = (g_pending_params.head + i) % kPendingParamsMax;
                g_publish_batch[i] = g_pending_params.lines[idx];
            }

            if (mqtt::PublishParameters(g_publish_batch.data(), g_pending_params.count)) {
                g_pending_params.head = 0U;
                g_pending_params.count = 0U;
            } else {
                ESP_LOGE(kTag, "Parameter MQTT publish failed");
            }
        }

        next_publish_us_ = now_us + kPublishPeriodUs;
    }
}

bool Logger::VerifyMqttPublish(const char* topic, const char* payload) noexcept {
    return mqtt::PublishRaw(topic, payload, "text/plain");
}

bool Logger::HasMonitorResult() const noexcept {
    return has_monitor_result_;
}

} // namespace logger
