#include "logger_internal.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "esp_eap_client.h"
#include "mqtt_client.h"
#include "mqtt5_client.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "sdkconfig.h"
#include "mqtt_log.hpp"

namespace logger::mqtt {

MqttLogBuffer g_mqtt_log_buffer;

namespace {

constexpr EventBits_t kWifiConnectedBit = BIT0;
constexpr EventBits_t kWifiFailedBit = BIT1;
constexpr EventBits_t kMqttConnectedBit = BIT0;
constexpr std::uint32_t kWifiConnectTimeoutMs = 20000U;
constexpr std::uint32_t kMqttConnectTimeoutMs = 10000U;
constexpr std::uint32_t kNtpSyncTimeoutMs = 15000U;
constexpr std::uint32_t kTxDrainDelayMs = 1000U;
constexpr std::uint32_t kMinValidEpoch = 1672531200U;

StaticEventGroup_t s_wifi_event_group_buffer{};
EventGroupHandle_t s_wifi_event_group = nullptr;

StaticEventGroup_t s_mqtt_event_group_buffer{};
EventGroupHandle_t s_mqtt_event_group = nullptr;

bool s_netif_initialized = false;
bool s_wifi_initialized = false;
bool s_events_registered = false;
bool s_nvs_initialized = false;
bool s_mqtt_ever_connected = false;
bool s_mqtt_connected = false;
std::uint8_t s_last_disconnect_reason = 0U;

esp_mqtt_client_handle_t s_client = nullptr;

esp_mqtt5_connection_property_config_t s_connect_property{};
esp_mqtt5_publish_property_config_t s_publish_property{};

static const char* kTag = "LOGGER_MQTT";

constexpr const char* kTopicBasePrefix = "ranting/";
constexpr const char* kTopicDelimiter = "/";
constexpr std::size_t kMaxNodeIdLen = 32U;
constexpr std::size_t kMaxTopicLen = 128U;

constexpr const char* kTopicSuffixParameters = "parameters";
constexpr const char* kTopicSuffixFailures = "failures";
constexpr const char* kTopicSuffixVerify = "verify";

static const char* kAdjectives[] = {
    "quiet", "bold", "calm", "cool", "crisp", "dawn", "deep", "dry",
    "dusk", "eager", "fair", "fast", "fierce", "fine", "flat", "fresh",
    "full", "glad", "gold", "good", "grand", "gray", "great", "green",
    "happy", "hard", "high", "hot", "keen", "kind", "late", "lean",
    "light", "long", "loud", "low", "lucky", "mild", "neat", "new",
    "nice", "noble", "odd", "old", "pale", "pink", "plain", "proud",
    "pure", "quick", "rare", "raw", "red", "rich", "rough", "royal",
    "safe", "sharp", "slim", "slow", "small", "smart", "soft", "warm"
};

static const char* kNouns[] = {
    "pine", "oak", "ash", "beech", "birch", "cedar", "elm", "fir",
    "hawk", "fox", "bear", "deer", "dove", "duck", "eagle", "falcon",
    "finch", "goose", "heron", "jay", "kite", "lark", "loon", "mink",
    "mole", "newt", "owl", "peak", "pond", "rail", "robin", "seal",
    "shrew", "skye", "sole", "spruce", "star", "stork", "swan", "teal",
    "tern", "toad", "trout", "vale", "vole", "wren", "yew", "zinc",
    "acre", "barn", "bell", "bend", "bloom", "breeze", "brook", "cairn",
    "cliff", "cloud", "coast", "cove", "creek", "dale", "dell", "fern"
};

constexpr std::size_t kAdjectiveCount = sizeof(kAdjectives) / sizeof(kAdjectives[0]);
constexpr std::size_t kNounCount = sizeof(kNouns) / sizeof(kNouns[0]);

static const char* kNvsNamespace = "logger";
static const char* kNvsKeyNodeId = "node_id";

const char* WifiReasonString(std::uint8_t reason) {
    switch (reason) {
        case WIFI_REASON_AUTH_FAIL:
            return "auth_fail";
        case WIFI_REASON_NO_AP_FOUND:
            return "no_ap";
        case WIFI_REASON_ASSOC_FAIL:
            return "assoc_fail";
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            return "handshake_timeout";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
            return "4way_timeout";
        case WIFI_REASON_BEACON_TIMEOUT:
            return "beacon_timeout";
        case WIFI_REASON_MIC_FAILURE:
            return "mic_failure";
        case WIFI_REASON_AUTH_EXPIRE:
            return "auth_expire";
        case WIFI_REASON_AUTH_LEAVE:
            return "auth_leave";
        case WIFI_REASON_ASSOC_EXPIRE:
            return "assoc_expire";
        case WIFI_REASON_ASSOC_LEAVE:
            return "assoc_leave";
        case WIFI_REASON_NOT_AUTHED:
            return "not_authed";
        case WIFI_REASON_NOT_ASSOCED:
            return "not_assoc";
        default:
            return "unknown";
    }
}

void WifiEventHandler(void*,
                      esp_event_base_t event_base,
                      int32_t event_id,
                      void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        g_mqtt_log_buffer.Log("WiFi: STA started, connecting...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        char buf[64];
        if (event_data != nullptr) {
            const auto* disconnected =
                static_cast<wifi_event_sta_disconnected_t*>(event_data);
            s_last_disconnect_reason = disconnected->reason;
            ESP_LOGW(kTag,
                     "WiFi disconnected: reason=%u (%s)",
                     static_cast<std::uint32_t>(disconnected->reason),
                     WifiReasonString(disconnected->reason));
            std::snprintf(buf, sizeof(buf), "WiFi: Disconnected, reason=%u (%s)",
                          disconnected->reason, WifiReasonString(disconnected->reason));
        } else {
            s_last_disconnect_reason = 0U;
            ESP_LOGW(kTag, "WiFi disconnected: reason=unknown");
            std::snprintf(buf, sizeof(buf), "WiFi: Disconnected, reason=unknown");
        }
        g_mqtt_log_buffer.Log(buf);
        if (s_wifi_event_group != nullptr) {
            xEventGroupSetBits(s_wifi_event_group, kWifiFailedBit);
        }
#if CONFIG_DASHBOARD_ENABLE
        esp_wifi_connect();
#endif
    }
}

void IpEventHandler(void*,
                    esp_event_base_t event_base,
                    int32_t event_id,
                    void* event_data) {
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        if (event_data != nullptr) {
            const auto* got_ip = static_cast<ip_event_got_ip_t*>(event_data);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "WiFi: Got IP " IPSTR, IP2STR(&got_ip->ip_info.ip));
            g_mqtt_log_buffer.Log(buf);
        } else {
            g_mqtt_log_buffer.Log("WiFi: Got IP address");
        }
        if (s_wifi_event_group != nullptr) {
            xEventGroupSetBits(s_wifi_event_group, kWifiConnectedBit);
        }
    }
}

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
                     "MQTT error: type=%d esp_err=0x%x (%s) tls=%d tls_flags=0x%x sock_errno=%d (%s)",
                     static_cast<int>(err->error_type),
                     static_cast<unsigned>(err->esp_tls_last_esp_err),
                     esp_err_to_name(err->esp_tls_last_esp_err),
                     err->esp_tls_stack_err,
                     err->esp_tls_cert_verify_flags,
                     err->esp_transport_sock_errno,
                     std::strerror(err->esp_transport_sock_errno));
            char buf[64];
            std::snprintf(buf, sizeof(buf), "MQTT: Error type=%d esp_err=0x%x",
                          static_cast<int>(err->error_type), static_cast<unsigned>(err->esp_tls_last_esp_err));
            g_mqtt_log_buffer.Log(buf);
        } else {
            ESP_LOGE(kTag, "MQTT error: missing event data");
            g_mqtt_log_buffer.Log("MQTT: Error missing event data");
        }
        return;
    }

    if (event_id == MQTT_EVENT_CONNECTED) {
        s_mqtt_ever_connected = true;
        s_mqtt_connected = true;
        g_mqtt_log_buffer.Log("MQTT: Connected to broker");
        xEventGroupSetBits(s_mqtt_event_group, kMqttConnectedBit);
    } else if (event_id == MQTT_EVENT_DISCONNECTED) {
        s_mqtt_connected = false;
        g_mqtt_log_buffer.Log("MQTT: Disconnected from broker");
        xEventGroupClearBits(s_mqtt_event_group, kMqttConnectedBit);
    }
}

bool InitNvs() {
    if (s_nvs_initialized) {
        return true;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        const esp_err_t erase_err = nvs_flash_erase();
        if (erase_err != ESP_OK) {
            ESP_LOGE(kTag, "NVS erase failed: %s", esp_err_to_name(erase_err));
            return false;
        }
        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        ESP_LOGE(kTag, "NVS init failed: %s", esp_err_to_name(err));
        return false;
    }

    s_nvs_initialized = true;
    return true;
}

bool ReadNodeIdFromNvs(char* out_buf, std::size_t buf_size) noexcept {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }
    std::size_t len = buf_size;
    err = nvs_get_str(handle, kNvsKeyNodeId, out_buf, &len);
    nvs_close(handle);
    return err == ESP_OK;
}

bool WriteNodeIdToNvs(const char* node_id) noexcept {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return false;
    }
    err = nvs_set_str(handle, kNvsKeyNodeId, node_id);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err == ESP_OK;
}

void GenerateNodeId(char* out_buf, std::size_t buf_size) noexcept {
    const std::size_t adj_idx = esp_random() % kAdjectiveCount;
    const std::size_t noun_idx = esp_random() % kNounCount;
    std::snprintf(out_buf, buf_size, "%s-%s", kAdjectives[adj_idx], kNouns[noun_idx]);
}

void ResolveNodeId(char* out_buf, std::size_t buf_size) noexcept {
    if (out_buf == nullptr || buf_size == 0U) {
        return;
    }

    const std::size_t kconfig_len = std::strlen(CONFIG_LOGGER_NODE_ID);
    if (kconfig_len > 0U) {
        std::strncpy(out_buf, CONFIG_LOGGER_NODE_ID, buf_size);
        out_buf[buf_size - 1U] = '\0';
        ESP_LOGI(kTag, "Node ID from Kconfig: %s", out_buf);
        return;
    }

    if (!InitNvs()) {
        ESP_LOGW(kTag, "NVS init failed, generating ephemeral node ID");
        GenerateNodeId(out_buf, buf_size);
        return;
    }

    if (ReadNodeIdFromNvs(out_buf, buf_size)) {
        ESP_LOGI(kTag, "Node ID from NVS: %s", out_buf);
        return;
    }

    GenerateNodeId(out_buf, buf_size);
    if (WriteNodeIdToNvs(out_buf)) {
        ESP_LOGI(kTag, "Node ID generated and stored: %s", out_buf);
    } else {
        ESP_LOGW(kTag, "Node ID generated but NVS write failed: %s", out_buf);
    }
}

bool InitWifiCore() {
    if (!InitNvs()) {
        return false;
    }

    if (!s_netif_initialized) {
        if (esp_netif_init() != ESP_OK) {
            ESP_LOGE(kTag, "esp_netif_init failed");
            return false;
        }
        const esp_err_t loop_err = esp_event_loop_create_default();
        if (loop_err != ESP_OK && loop_err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(kTag, "Event loop init failed: %s", esp_err_to_name(loop_err));
            return false;
        }
        esp_netif_create_default_wifi_sta();
        s_netif_initialized = true;
    }

    if (!s_wifi_initialized) {
        const wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        const esp_err_t err = esp_wifi_init(&cfg);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "WiFi init failed: %s", esp_err_to_name(err));
            return false;
        }
        s_wifi_initialized = true;
    }

    if (!s_events_registered) {
        if (esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiEventHandler, nullptr) != ESP_OK) {
            ESP_LOGE(kTag, "WiFi event handler register failed");
            return false;
        }
        if (esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &IpEventHandler, nullptr) != ESP_OK) {
            ESP_LOGE(kTag, "IP event handler register failed");
            return false;
        }
        s_events_registered = true;
    }

    if (s_wifi_event_group == nullptr) {
        s_wifi_event_group = xEventGroupCreateStatic(&s_wifi_event_group_buffer);
    }
    if (s_mqtt_event_group == nullptr) {
        s_mqtt_event_group = xEventGroupCreateStatic(&s_mqtt_event_group_buffer);
    }

    return s_wifi_event_group != nullptr && s_mqtt_event_group != nullptr;
}

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

bool ConnectWifi() {
#if CONFIG_DASHBOARD_ENABLE
    if (s_wifi_event_group != nullptr) {
        const EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                                     kWifiConnectedBit,
                                                     pdFALSE,
                                                     pdFALSE,
                                                     pdMS_TO_TICKS(kWifiConnectTimeoutMs));
        if ((bits & kWifiConnectedBit) != 0U) {
            return true;
        }
    }
    return false;
#endif

    if (!InitWifiCore()) {
        return false;
    }

    if (std::strlen(CONFIG_LOGGER_WIFI_SSID) == 0U) {
        ESP_LOGE(kTag, "WiFi SSID not set");
        g_mqtt_log_buffer.Log("WiFi: Error SSID not set");
        return false;
    }

    wifi_config_t wifi_cfg{};
    std::strncpy(reinterpret_cast<char*>(wifi_cfg.sta.ssid),
                 CONFIG_LOGGER_WIFI_SSID,
                 sizeof(wifi_cfg.sta.ssid));
    wifi_cfg.sta.ssid[sizeof(wifi_cfg.sta.ssid) - 1U] = '\0';
    
#if CONFIG_LOGGER_WIFI_ENT_ENABLE
    wifi_cfg.sta.password[0] = '\0';
#else
    std::strncpy(reinterpret_cast<char*>(wifi_cfg.sta.password),
                 CONFIG_LOGGER_WIFI_PASSWORD,
                 sizeof(wifi_cfg.sta.password));
    wifi_cfg.sta.password[sizeof(wifi_cfg.sta.password) - 1U] = '\0';
#endif

    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
        ESP_LOGE(kTag, "WiFi set mode failed");
        g_mqtt_log_buffer.Log("WiFi: Error set mode failed");
        return false;
    }
    if (esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg) != ESP_OK) {
        ESP_LOGE(kTag, "WiFi set config failed");
        g_mqtt_log_buffer.Log("WiFi: Error set config failed");
        return false;
    }

#if CONFIG_LOGGER_WIFI_ENT_ENABLE
    g_mqtt_log_buffer.Log("WiFi: Enterprise config starting...");
    const char* identity = CONFIG_LOGGER_WIFI_ENT_IDENTITY;
    const char* username = CONFIG_LOGGER_WIFI_ENT_USERNAME;
    if (std::strlen(username) == 0U) {
        username = identity;
    }
    if (std::strlen(identity) > 0U) {
        ESP_ERROR_CHECK(esp_eap_client_set_identity(
            (const unsigned char *)identity,
            std::strlen(identity)));
    }
    if (std::strlen(username) > 0U) {
        ESP_ERROR_CHECK(esp_eap_client_set_username(
            (const unsigned char *)username,
            std::strlen(username)));
    }
    if (std::strlen(CONFIG_LOGGER_WIFI_ENT_PASSWORD) > 0U) {
        ESP_ERROR_CHECK(esp_eap_client_set_password(
            (const unsigned char *)CONFIG_LOGGER_WIFI_ENT_PASSWORD,
            std::strlen(CONFIG_LOGGER_WIFI_ENT_PASSWORD)));
    }
    ESP_ERROR_CHECK(esp_wifi_sta_enterprise_enable());
    g_mqtt_log_buffer.Log("WiFi: Enterprise PEAP enabled");
#endif

    xEventGroupClearBits(s_wifi_event_group, kWifiConnectedBit | kWifiFailedBit);
    s_last_disconnect_reason = 0U;
    
    char log_buf[64];
    std::snprintf(log_buf, sizeof(log_buf), "WiFi: Connecting to SSID %s...", CONFIG_LOGGER_WIFI_SSID);
    g_mqtt_log_buffer.Log(log_buf);
    
    if (esp_wifi_start() != ESP_OK) {
        ESP_LOGE(kTag, "WiFi start failed");
        g_mqtt_log_buffer.Log("WiFi: Error start failed");
        return false;
    }

    const EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                                 kWifiConnectedBit | kWifiFailedBit,
                                                 pdTRUE,
                                                 pdFALSE,
                                                 pdMS_TO_TICKS(kWifiConnectTimeoutMs));
    if ((bits & kWifiFailedBit) != 0U) {
        ESP_LOGE(kTag,
                 "WiFi connect failed: reason=%u (%s)",
                 static_cast<std::uint32_t>(s_last_disconnect_reason),
                 WifiReasonString(s_last_disconnect_reason));
        return false;
    }
    if ((bits & kWifiConnectedBit) == 0U) {
        ESP_LOGE(kTag, "WiFi connect timeout");
        g_mqtt_log_buffer.Log("WiFi: Error connect timeout");
        return false;
    }

    return true;
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

bool EnsureMqttClient() {
    if (s_client != nullptr) {
        return true;
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
        return false;
    }

    if (esp_mqtt_client_register_event(s_client,
                                       static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID),
                                       MqttEventHandler,
                                       nullptr) != ESP_OK) {
        ESP_LOGE(kTag, "MQTT event handler register failed");
        return false;
    }

    return true;
}

bool PublishLines(const char* topic, const CsvLine* lines, std::size_t count, const char* content_type) {
    if (topic == nullptr || lines == nullptr) {
        return false;
    }

    if (!ConnectWifi()) {
        return false;
    }
    SyncTime();

    if (!s_mqtt_connected) {
        xEventGroupClearBits(s_mqtt_event_group, kMqttConnectedBit);
        esp_err_t start_err;
        if (s_mqtt_ever_connected) {
            start_err = esp_mqtt_client_reconnect(s_client);
            if (start_err != ESP_OK) {
                start_err = esp_mqtt_client_start(s_client);
            }
        } else {
            start_err = esp_mqtt_client_start(s_client);
        }
        if (start_err != ESP_OK) {
            ESP_LOGE(kTag, "MQTT start/reconnect failed");
            return false;
        }
    }

    const EventBits_t bits = xEventGroupWaitBits(s_mqtt_event_group,
                                                 kMqttConnectedBit,
                                                 pdTRUE,
                                                 pdFALSE,
                                                 pdMS_TO_TICKS(kMqttConnectTimeoutMs));
    if ((bits & kMqttConnectedBit) == 0U) {
        ESP_LOGE(kTag, "MQTT connect timeout");
        return false;
    }

    bool ok = true;
    for (std::size_t i = 0U; i < count; ++i) {
        std::size_t payload_len = lines[i].length;
        if (payload_len > 0U && lines[i].buffer[payload_len - 1U] == '\n') {
            --payload_len;
        }

        s_publish_property.payload_format_indicator = true;
        s_publish_property.content_type = content_type;
        if (esp_mqtt5_client_set_publish_property(s_client, &s_publish_property) != ESP_OK) {
            ESP_LOGE(kTag, "MQTT5 publish property set failed");
            ok = false;
            continue;
        }

        const int msg_id = esp_mqtt_client_publish(s_client,
                                                   topic,
                                                   lines[i].buffer.data(),
                                                   static_cast<int>(payload_len),
                                                   CONFIG_LOGGER_MQTT_QOS,
                                                   0);
        if (msg_id < 0) {
            ESP_LOGE(kTag, "MQTT publish failed");
            ok = false;
        }
    }

    vTaskDelay(pdMS_TO_TICKS(kTxDrainDelayMs));
    return ok;
}

bool PublishRawInternal(const char* topic, const char* payload, const char* content_type) {
    if (topic == nullptr || payload == nullptr) {
        return false;
    }

    if (!ConnectWifi()) {
        return false;
    }
    SyncTime();

    if (!s_mqtt_connected) {
        xEventGroupClearBits(s_mqtt_event_group, kMqttConnectedBit);
        esp_err_t start_err;
        if (s_mqtt_ever_connected) {
            start_err = esp_mqtt_client_reconnect(s_client);
            if (start_err != ESP_OK) {
                start_err = esp_mqtt_client_start(s_client);
            }
        } else {
            start_err = esp_mqtt_client_start(s_client);
        }
        if (start_err != ESP_OK) {
            ESP_LOGE(kTag, "MQTT start/reconnect failed");
            return false;
        }
    }

    const EventBits_t bits = xEventGroupWaitBits(s_mqtt_event_group,
                                                 kMqttConnectedBit,
                                                 pdTRUE,
                                                 pdFALSE,
                                                 pdMS_TO_TICKS(kMqttConnectTimeoutMs));
    if ((bits & kMqttConnectedBit) == 0U) {
        ESP_LOGE(kTag, "MQTT connect timeout");
        return false;
    }

    const char* resolved_type = (content_type != nullptr) ? content_type : "text/plain";
    s_publish_property.payload_format_indicator = true;
    s_publish_property.content_type = resolved_type;
    if (esp_mqtt5_client_set_publish_property(s_client, &s_publish_property) != ESP_OK) {
        ESP_LOGE(kTag, "MQTT5 publish property set failed");
        return false;
    }

    const int msg_id = esp_mqtt_client_publish(s_client,
                                               topic,
                                               payload,
                                               0,
                                               CONFIG_LOGGER_MQTT_QOS,
                                               0);
    if (msg_id < 0) {
        ESP_LOGE(kTag, "MQTT publish failed");
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(kTxDrainDelayMs));
    return true;
}
#if CONFIG_DASHBOARD_ENABLE
bool StartWifiPersistent() {
    if (!InitWifiCore()) {
        return false;
    }

    if (std::strlen(CONFIG_LOGGER_WIFI_SSID) == 0U) {
        ESP_LOGE(kTag, "WiFi SSID not set");
        g_mqtt_log_buffer.Log("WiFi: Error SSID not set");
        return false;
    }

    wifi_config_t wifi_cfg{};
    std::strncpy(reinterpret_cast<char*>(wifi_cfg.sta.ssid),
                 CONFIG_LOGGER_WIFI_SSID,
                 sizeof(wifi_cfg.sta.ssid));
    wifi_cfg.sta.ssid[sizeof(wifi_cfg.sta.ssid) - 1U] = '\0';
    
#if CONFIG_LOGGER_WIFI_ENT_ENABLE
    wifi_cfg.sta.password[0] = '\0';
#else
    std::strncpy(reinterpret_cast<char*>(wifi_cfg.sta.password),
                 CONFIG_LOGGER_WIFI_PASSWORD,
                 sizeof(wifi_cfg.sta.password));
    wifi_cfg.sta.password[sizeof(wifi_cfg.sta.password) - 1U] = '\0';
#endif

    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
        ESP_LOGE(kTag, "WiFi set mode failed");
        g_mqtt_log_buffer.Log("WiFi: Error set mode failed");
        return false;
    }
    if (esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg) != ESP_OK) {
        ESP_LOGE(kTag, "WiFi set config failed");
        g_mqtt_log_buffer.Log("WiFi: Error set config failed");
        return false;
    }

#if CONFIG_LOGGER_WIFI_ENT_ENABLE
    g_mqtt_log_buffer.Log("WiFi: Enterprise config starting...");
    const char* identity = CONFIG_LOGGER_WIFI_ENT_IDENTITY;
    const char* username = CONFIG_LOGGER_WIFI_ENT_USERNAME;
    if (std::strlen(username) == 0U) {
        username = identity;
    }
    if (std::strlen(identity) > 0U) {
        ESP_ERROR_CHECK(esp_eap_client_set_identity(
            (const unsigned char *)identity,
            std::strlen(identity)));
    }
    if (std::strlen(username) > 0U) {
        ESP_ERROR_CHECK(esp_eap_client_set_username(
            (const unsigned char *)username,
            std::strlen(username)));
    }
    if (std::strlen(CONFIG_LOGGER_WIFI_ENT_PASSWORD) > 0U) {
        ESP_ERROR_CHECK(esp_eap_client_set_password(
            (const unsigned char *)CONFIG_LOGGER_WIFI_ENT_PASSWORD,
            std::strlen(CONFIG_LOGGER_WIFI_ENT_PASSWORD)));
    }
    ESP_ERROR_CHECK(esp_wifi_sta_enterprise_enable());
    g_mqtt_log_buffer.Log("WiFi: Enterprise PEAP enabled");
#endif

    xEventGroupClearBits(s_wifi_event_group, kWifiConnectedBit | kWifiFailedBit);
    s_last_disconnect_reason = 0U;
    
    char log_buf[64];
    std::snprintf(log_buf, sizeof(log_buf), "WiFi: Connecting to SSID %s...", CONFIG_LOGGER_WIFI_SSID);
    g_mqtt_log_buffer.Log(log_buf);
    
    if (esp_wifi_start() != ESP_OK) {
        ESP_LOGE(kTag, "WiFi start failed");
        g_mqtt_log_buffer.Log("WiFi: Error start failed");
        return false;
    }

    return true;
}
#endif

} // namespace

static char s_node_id[kMaxNodeIdLen]{};
static bool s_node_id_resolved = false;

const char* GetNodeId() noexcept {
    if (!s_node_id_resolved) {
        ResolveNodeId(s_node_id, sizeof(s_node_id));
        s_node_id_resolved = true;
    }
    return s_node_id;
}

void BuildTopic(char* buf, std::size_t buf_size, const char* node_id, const char* datatype) noexcept {
    std::snprintf(buf, buf_size, "%s%s%s%s", kTopicBasePrefix, node_id, kTopicDelimiter, datatype);
}

const char* GetTopic(const char* datatype) noexcept {
    static char s_topic_buffer[kMaxTopicLen];
    BuildTopic(s_topic_buffer, sizeof(s_topic_buffer), GetNodeId(), datatype);
    return s_topic_buffer;
}

bool SyncTimeOnce() noexcept {
    if (!ConnectWifi()) {
        return false;
    }
    const bool synced = SyncTime();
#if !CONFIG_DASHBOARD_ENABLE
    esp_wifi_disconnect();
    esp_wifi_stop();
#endif
    return synced;
}

bool Init() noexcept {
#if CONFIG_DASHBOARD_ENABLE
    if (!StartWifiPersistent()) {
        return false;
    }
#else
    if (!InitWifiCore()) {
        return false;
    }
#endif
    return EnsureMqttClient();
}

bool PublishParameters(const CsvLine* lines, std::size_t count) noexcept {
    if (count == 0U) {
        return true;
    }

    const bool ok = PublishLines(GetTopic(kTopicSuffixParameters), lines, count, "application/json");
#if !CONFIG_DASHBOARD_ENABLE
    esp_wifi_disconnect();
    esp_wifi_stop();
#endif
    return ok;
}

bool PublishFailure(const CsvLine& line) noexcept {
    const bool ok = PublishLines(GetTopic(kTopicSuffixFailures), &line, 1U, "text/csv");
#if !CONFIG_DASHBOARD_ENABLE
    esp_wifi_disconnect();
    esp_wifi_stop();
#endif
    return ok;
}

bool PublishRaw(const char* topic, const char* payload, const char* content_type) noexcept {
    const bool ok = PublishRawInternal(topic, payload, content_type);
#if !CONFIG_DASHBOARD_ENABLE
    esp_wifi_disconnect();
    esp_wifi_stop();
#endif
    return ok;
}

} // namespace logger::mqtt
