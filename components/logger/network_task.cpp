/// @file network_task.cpp
/// @brief Dedicated FreeRTOS task for WiFi/MQTT publish operations.
/// @ingroup logger

#include "network_task.hpp"
#include "network_strategy.hpp"
#include "outbox.hpp"
#include "logger_internal.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "mqtt5_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace logger::network_task {

namespace {

constexpr std::uint32_t kMqttConnectTimeoutMs = 10000U;
constexpr std::uint32_t kNtpSyncTimeoutMs = 15000U;
constexpr std::uint32_t kMinValidEpoch = 1672531200U;
constexpr EventBits_t kMqttConnectedBit = BIT0;
constexpr std::size_t kTaskStackSize = 6144U;
constexpr std::uint64_t kParameterPeriodUs =
    static_cast<std::uint64_t>(CONFIG_LOGGER_WIFI_PERIOD_HOURS) * 60ULL * 1000000ULL;
constexpr UBaseType_t kTaskPriority = 3U;
constexpr BaseType_t kTaskCore = 0;

static const char* kTag = "NET_TASK";
const char* s_mount_point = nullptr;
TaskHandle_t s_task_handle = nullptr;

StaticEventGroup_t s_mqtt_event_group_buffer{};
EventGroupHandle_t s_mqtt_event_group = nullptr;

esp_mqtt_client_handle_t s_client = nullptr;
esp_mqtt5_connection_property_config_t s_connect_property{};
esp_mqtt5_publish_property_config_t s_publish_property{};
outbox::FileEntry s_pending_entries[outbox::kMaxPendingFiles]{};
outbox::FileEntry s_publish_entries[outbox::kMaxPendingFiles]{};
char s_pending_path[outbox::kPathMax]{};
char s_line_buf[512]{};
std::uint64_t s_last_parameter_publish_us{0U};

// ============================================================================
// Backoff State
// ============================================================================

struct BackoffState {
    std::uint32_t current_ms{60000U};
    std::uint64_t backoff_until_us{0U};

    static constexpr std::uint32_t kBackoffInitialMs = 60000U;
    static constexpr std::uint32_t kBackoffMaxMs = 3600000U;
    static constexpr std::uint32_t kBackoffJitterMs = 5000U;

    bool ShouldSkip(std::uint64_t now_us) const {
        return now_us < backoff_until_us;
    }

    void OnFailure(std::uint64_t now_us) {
        current_ms *= 2U;
        if (current_ms > kBackoffMaxMs) {
            current_ms = kBackoffMaxMs;
        }

        const std::uint32_t jitter = static_cast<std::uint32_t>(esp_random()) % kBackoffJitterMs;
        const std::uint64_t delay_us = static_cast<std::uint64_t>(current_ms + jitter) * 1000ULL;
        backoff_until_us = now_us + delay_us;

        ESP_LOGW(kTag, "Backoff: next attempt in %lu ms",
                 static_cast<unsigned long>(current_ms + jitter));
    }

    void OnSuccess() {
        current_ms = kBackoffInitialMs;
        backoff_until_us = 0U;
    }
};

BackoffState s_backoff{};

void LogStackHighWater(const char* stage) {
    const UBaseType_t words = uxTaskGetStackHighWaterMark(nullptr);
    const std::uint32_t bytes = static_cast<std::uint32_t>(words) * sizeof(StackType_t);
    ESP_LOGI(kTag, "network_task stack_high_water stage=%s bytes=%lu",
             stage,
             static_cast<unsigned long>(bytes));
}

bool ParameterCadenceDue(std::uint64_t now_us) {
    if (kParameterPeriodUs == 0U) {
        return true;
    }
    if (s_last_parameter_publish_us == 0U) {
        s_last_parameter_publish_us = now_us;
        return false;
    }
    return (now_us - s_last_parameter_publish_us) >= kParameterPeriodUs;
}

std::size_t SelectPublishEntries(const outbox::FileEntry* entries,
                                 std::size_t file_count,
                                 bool params_due,
                                 bool& out_has_failure,
                                 bool& out_has_params) {
    out_has_failure = false;
    out_has_params = false;
    std::size_t publish_count = 0U;

    for (std::size_t i = 0U; i < file_count && publish_count < outbox::kMaxPendingFiles; ++i) {
        if (entries[i].is_failure) {
            out_has_failure = true;
            s_publish_entries[publish_count] = entries[i];
            ++publish_count;
        } else {
            out_has_params = true;
            if (params_due) {
                s_publish_entries[publish_count] = entries[i];
                ++publish_count;
            }
        }
    }

    return publish_count;
}

std::size_t SelectPublishEntriesToBuffer(const outbox::FileEntry* entries,
                                         std::size_t file_count,
                                         bool params_due,
                                         outbox::FileEntry* out_entries,
                                         std::size_t out_capacity,
                                         bool& out_has_failure,
                                         bool& out_has_params) {
    out_has_failure = false;
    out_has_params = false;
    std::size_t publish_count = 0U;
    if (entries == nullptr || out_entries == nullptr) {
        return 0U;
    }

    for (std::size_t i = 0U; i < file_count && publish_count < out_capacity; ++i) {
        if (entries[i].is_failure) {
            out_has_failure = true;
            out_entries[publish_count] = entries[i];
            ++publish_count;
        } else {
            out_has_params = true;
            if (params_due) {
                out_entries[publish_count] = entries[i];
                ++publish_count;
            }
        }
    }

    return publish_count;
}

// ============================================================================
// MQTT Event Handler
// ============================================================================

void MqttEventHandler(void*,
                      esp_event_base_t,
                      int32_t event_id,
                      void* event_data) {
    if (s_mqtt_event_group == nullptr) {
        return;
    }

    if (event_id == MQTT_EVENT_ERROR) {
        const auto* event = static_cast<esp_mqtt_event_handle_t>(event_data);
        if (event != nullptr && event->error_handle != nullptr) {
            const esp_mqtt_error_codes_t* err = event->error_handle;
            ESP_LOGE(kTag,
                     "MQTT error: type=%d esp_err=0x%x (%s) tls=%d",
                     static_cast<int>(err->error_type),
                     static_cast<unsigned>(err->esp_tls_last_esp_err),
                     esp_err_to_name(err->esp_tls_last_esp_err),
                     err->esp_tls_stack_err);
        }
        return;
    }

    if (event_id == MQTT_EVENT_CONNECTED) {
        xEventGroupSetBits(s_mqtt_event_group, kMqttConnectedBit);
    } else if (event_id == MQTT_EVENT_DISCONNECTED) {
        xEventGroupClearBits(s_mqtt_event_group, kMqttConnectedBit);
    }
}

// ============================================================================
// NTP Time Sync
// ============================================================================

bool WaitForTimeSync() {
    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS(kNtpSyncTimeoutMs);
    while ((xTaskGetTickCount() - start) < timeout) {
        std::time_t now = 0;
        std::time(&now);
        if (static_cast<std::uint32_t>(now) >= kMinValidEpoch) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return false;
}

bool SyncTime() {
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, CONFIG_LOGGER_NTP_SERVER);
    esp_sntp_init();
    const bool synced = WaitForTimeSync();
    esp_sntp_stop();
    if (!synced) {
        ESP_LOGW(kTag, "NTP sync timeout");
    }
    return synced;
}

// ============================================================================
// MQTT Client Lifecycle
// ============================================================================

bool EnsureMqttClient() {
    if (s_client != nullptr) {
        return true;
    }

    if (s_mqtt_event_group == nullptr) {
        s_mqtt_event_group = xEventGroupCreateStatic(&s_mqtt_event_group_buffer);
        if (s_mqtt_event_group == nullptr) {
            ESP_LOGE(kTag, "MQTT event group create failed");
            return false;
        }
    }

    esp_mqtt_client_config_t mqtt_cfg{};
    mqtt_cfg.broker.address.uri = CONFIG_LOGGER_MQTT_URI;
    mqtt_cfg.credentials.username = CONFIG_LOGGER_MQTT_USERNAME;
    mqtt_cfg.credentials.authentication.password = CONFIG_LOGGER_MQTT_PASSWORD;
    mqtt_cfg.credentials.client_id = CONFIG_LOGGER_MQTT_CLIENT_ID;
    mqtt_cfg.session.protocol_ver = MQTT_PROTOCOL_V_5;
    mqtt_cfg.network.disable_auto_reconnect = true;

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client == nullptr) {
        ESP_LOGE(kTag, "MQTT client init failed");
        return false;
    }

    s_connect_property.session_expiry_interval = 0U;
    s_connect_property.request_problem_info = true;
    if (esp_mqtt5_client_set_connect_property(s_client, &s_connect_property) != ESP_OK) {
        ESP_LOGE(kTag, "MQTT5 connect property set failed");
        esp_mqtt_client_destroy(s_client);
        s_client = nullptr;
        return false;
    }

    if (esp_mqtt_client_register_event(s_client,
                                       static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID),
                                       MqttEventHandler,
                                       nullptr) != ESP_OK) {
        ESP_LOGE(kTag, "MQTT event handler register failed");
        esp_mqtt_client_destroy(s_client);
        s_client = nullptr;
        return false;
    }

    return true;
}

// ============================================================================
// Publish One Line
// ============================================================================

bool PublishLine(const char* topic, const char* line, std::size_t len, const char* content_type) {
    if (topic == nullptr || line == nullptr || len == 0U) {
        return false;
    }

    std::size_t payload_len = len;
    if (payload_len > 0U && line[payload_len - 1U] == '\n') {
        --payload_len;
    }

    s_publish_property.payload_format_indicator = true;
    s_publish_property.content_type = content_type;
    if (esp_mqtt5_client_set_publish_property(s_client, &s_publish_property) != ESP_OK) {
        ESP_LOGE(kTag, "MQTT5 publish property set failed");
        return false;
    }

    const int msg_id = esp_mqtt_client_publish(s_client,
                                               topic,
                                               line,
                                               static_cast<int>(payload_len),
                                               CONFIG_LOGGER_MQTT_QOS,
                                               0);
    if (msg_id < 0) {
        ESP_LOGE(kTag, "MQTT publish failed");
        return false;
    }
    return true;
}

// ============================================================================
// Process One Pending File
// ============================================================================

bool ProcessFile(const outbox::FileEntry& entry) {
    const int path_len = std::snprintf(s_pending_path, sizeof(s_pending_path),
                                        "%s/outbox/pending/%s",
                                        s_mount_point,
                                        entry.filename.data());
    if (path_len <= 0 || static_cast<std::size_t>(path_len) >= sizeof(s_pending_path)) {
        return false;
    }

    FILE* file = std::fopen(s_pending_path, "r");
    if (file == nullptr) {
        ESP_LOGW(kTag, "Open pending file failed: %s", s_pending_path);
        return false;
    }

    bool all_ok = true;
    const char* content_type = entry.is_failure ? "application/json" : "application/json";
    const char* datatype = entry.is_failure ? "failures" : "parameters";
    const char* topic = mqtt::GetTopic(datatype);

    while (std::fgets(s_line_buf, static_cast<int>(sizeof(s_line_buf)), file) != nullptr) {
        const std::size_t len = std::strlen(s_line_buf);
        if (len == 0U) {
            continue;
        }
        if (!PublishLine(topic, s_line_buf, len, content_type)) {
            all_ok = false;
            ESP_LOGW(kTag, "Failed to publish line from %s", entry.filename.data());
        }
    }

    std::fclose(file);
    return all_ok;
}

// ============================================================================
// Task Loop
// ============================================================================

void TaskLoop(void*) {
    ESP_LOGI(kTag, "Network task started");
    s_last_parameter_publish_us = static_cast<std::uint64_t>(esp_timer_get_time());
    LogStackHighWater("start");

    while (true) {
        // wait for notification or timeout (for periodic scan)
        const std::uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
        static_cast<void>(notified);

        std::size_t file_count = 0U;
        const std::uint64_t now_us = static_cast<std::uint64_t>(esp_timer_get_time());
        const bool params_due = ParameterCadenceDue(now_us);
        if (params_due && !outbox::SealParameterFile()) {
            ESP_LOGW(kTag, "Parameter seal failed");
        }

        if (!outbox::GetPendingFiles(s_pending_entries, file_count) || file_count == 0U) {
            continue;
        }

        bool has_failure = false;
        bool has_params = false;
        const std::size_t publish_count = SelectPublishEntries(s_pending_entries,
                                                               file_count,
                                                               params_due,
                                                               has_failure,
                                                               has_params);
        if (publish_count == 0U) {
            ESP_LOGD(kTag, "Skipping publish: params_due=%d has_params=%d has_failure=%d",
                     params_due ? 1 : 0,
                     has_params ? 1 : 0,
                     has_failure ? 1 : 0);
            continue;
        }

        // check backoff
        if (s_backoff.ShouldSkip(now_us)) {
            ESP_LOGD(kTag, "Skipping publish — in backoff period");
            continue;
        }

        // connect WiFi
        LogStackHighWater("pre_connect");
        if (!network::EnsureConnected()) {
            ESP_LOGE(kTag, "WiFi connect failed");
            s_backoff.OnFailure(static_cast<std::uint64_t>(esp_timer_get_time()));
            continue;
        }
        LogStackHighWater("post_connect");

        // sync NTP after WiFi connect
        SyncTime();

        // ensure MQTT client
        if (!EnsureMqttClient()) {
            ESP_LOGE(kTag, "MQTT client init failed");
            s_backoff.OnFailure(static_cast<std::uint64_t>(esp_timer_get_time()));
            network::ReleaseConnection();
            continue;
        }

        // start MQTT and wait for connect
        xEventGroupClearBits(s_mqtt_event_group, kMqttConnectedBit);
        if (esp_mqtt_client_start(s_client) != ESP_OK) {
            ESP_LOGE(kTag, "MQTT start failed");
            s_backoff.OnFailure(static_cast<std::uint64_t>(esp_timer_get_time()));
            network::ReleaseConnection();
            continue;
        }

        const EventBits_t bits = xEventGroupWaitBits(s_mqtt_event_group,
                                                     kMqttConnectedBit,
                                                     pdTRUE,
                                                     pdFALSE,
                                                     pdMS_TO_TICKS(kMqttConnectTimeoutMs));
        if ((bits & kMqttConnectedBit) == 0U) {
            ESP_LOGE(kTag, "MQTT connect timeout");
            esp_mqtt_client_stop(s_client);
            s_backoff.OnFailure(static_cast<std::uint64_t>(esp_timer_get_time()));
            network::ReleaseConnection();
            continue;
        }

        // publish all pending files
        bool all_success = true;
        bool published_params = false;
        LogStackHighWater("pre_publish_batch");
        for (std::size_t i = 0U; i < publish_count; ++i) {
            ESP_LOGI(kTag, "Publishing: %s", s_publish_entries[i].filename.data());
            if (ProcessFile(s_publish_entries[i])) {
                if (!outbox::MarkSent(s_publish_entries[i].filename.data())) {
                    all_success = false;
                    ESP_LOGW(kTag, "MarkSent failed for %s", s_publish_entries[i].filename.data());
                } else if (!s_publish_entries[i].is_failure) {
                    published_params = true;
                }
            } else {
                all_success = false;
            }
        }
        LogStackHighWater("post_publish_batch");

        // stop MQTT client
        esp_mqtt_client_stop(s_client);

        // update backoff
        if (all_success && publish_count > 0U) {
            s_backoff.OnSuccess();
            if (published_params) {
                s_last_parameter_publish_us = static_cast<std::uint64_t>(esp_timer_get_time());
            }
            ESP_LOGI(kTag, "Publish batch succeeded. Files: %u", static_cast<unsigned>(publish_count));
        } else {
            s_backoff.OnFailure(static_cast<std::uint64_t>(esp_timer_get_time()));
        }

        // prune sent directory
        LogStackHighWater("pre_cleanup");
        static_cast<void>(outbox::PruneSent(10U));

        // release WiFi connection
        network::ReleaseConnection();
        LogStackHighWater("post_cleanup");

        ESP_LOGI(kTag, "Publish cycle complete");
    }
}

} // namespace

// ============================================================================
// Public API
// ============================================================================

bool Init(const char* mount_point) noexcept {
    if (mount_point == nullptr || std::strlen(mount_point) == 0U) {
        ESP_LOGE(kTag, "SD mount point not set");
        return false;
    }
    s_mount_point = mount_point;
    return true;
}

bool Start() noexcept {
    const BaseType_t ret = xTaskCreatePinnedToCore(
        TaskLoop,
        "network_task",
        kTaskStackSize,
        nullptr,
        kTaskPriority,
        &s_task_handle,
        kTaskCore);
    if (ret != pdPASS) {
        const std::uint32_t free_internal = static_cast<std::uint32_t>(
            heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        const std::uint32_t largest_block = static_cast<std::uint32_t>(
            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        ESP_LOGE(kTag, "Failed to create network_task (stack=%u free=%lu largest=%lu)",
                 static_cast<unsigned>(kTaskStackSize),
                 static_cast<unsigned long>(free_internal),
                 static_cast<unsigned long>(largest_block));
        return false;
    }
    ESP_LOGI(kTag, "network_task started on core %d, priority %u", kTaskCore, kTaskPriority);
    return true;
}

void EnqueueNotify() noexcept {
    if (s_task_handle != nullptr) {
        xTaskNotifyGive(s_task_handle);
    }
}

std::size_t SelectPublishEntriesForTest(const outbox::FileEntry* entries,
                                        std::size_t file_count,
                                        bool params_due,
                                        outbox::FileEntry* out_entries,
                                        std::size_t out_capacity) noexcept {
    bool has_failure = false;
    bool has_params = false;
    return SelectPublishEntriesToBuffer(entries,
                                        file_count,
                                        params_due,
                                        out_entries,
                                        out_capacity,
                                        has_failure,
                                        has_params);
}

} // namespace logger::network_task
