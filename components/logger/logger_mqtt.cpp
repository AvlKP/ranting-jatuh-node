#include "logger_internal.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "mqtt5_client.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "sdkconfig.h"

namespace logger::mqtt {

namespace {

constexpr EventBits_t kWifiConnectedBit = BIT0;
constexpr EventBits_t kWifiFailedBit = BIT1;
constexpr EventBits_t kMqttConnectedBit = BIT0;
constexpr std::uint32_t kWifiConnectTimeoutMs = 20000U;
constexpr std::uint32_t kMqttConnectTimeoutMs = 10000U;
constexpr std::uint32_t kNtpSyncTimeoutMs = 15000U;
constexpr std::uint32_t kMinValidEpoch = 1672531200U;

StaticEventGroup_t s_wifi_event_group_buffer{};
EventGroupHandle_t s_wifi_event_group = nullptr;

StaticEventGroup_t s_mqtt_event_group_buffer{};
EventGroupHandle_t s_mqtt_event_group = nullptr;

bool s_netif_initialized = false;
bool s_wifi_initialized = false;
bool s_events_registered = false;
bool s_nvs_initialized = false;

esp_mqtt_client_handle_t s_client = nullptr;

esp_mqtt5_connection_property_config_t s_connect_property{};
esp_mqtt5_publish_property_config_t s_publish_property{};

void WifiEventHandler(void*,
                      esp_event_base_t event_base,
                      int32_t event_id,
                      void*) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_event_group != nullptr) {
            xEventGroupSetBits(s_wifi_event_group, kWifiFailedBit);
        }
    }
}

void IpEventHandler(void*,
                    esp_event_base_t event_base,
                    int32_t event_id,
                    void*) {
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        if (s_wifi_event_group != nullptr) {
            xEventGroupSetBits(s_wifi_event_group, kWifiConnectedBit);
        }
    }
}

void MqttEventHandler(void*,
                      esp_event_base_t,
                      int32_t event_id,
                      void*) {
    if (s_mqtt_event_group == nullptr) {
        return;
    }

    if (event_id == MQTT_EVENT_CONNECTED) {
        xEventGroupSetBits(s_mqtt_event_group, kMqttConnectedBit);
    } else if (event_id == MQTT_EVENT_DISCONNECTED) {
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
            std::printf("logger: nvs erase failed: %s\n", esp_err_to_name(erase_err));
            return false;
        }
        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        std::printf("logger: nvs init failed: %s\n", esp_err_to_name(err));
        return false;
    }

    s_nvs_initialized = true;
    return true;
}

bool InitWifiCore() {
    if (!InitNvs()) {
        return false;
    }

    if (!s_netif_initialized) {
        if (esp_netif_init() != ESP_OK) {
            std::printf("logger: esp_netif_init failed\n");
            return false;
        }
        const esp_err_t loop_err = esp_event_loop_create_default();
        if (loop_err != ESP_OK && loop_err != ESP_ERR_INVALID_STATE) {
            std::printf("logger: event loop init failed: %s\n", esp_err_to_name(loop_err));
            return false;
        }
        esp_netif_create_default_wifi_sta();
        s_netif_initialized = true;
    }

    if (!s_wifi_initialized) {
        const wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        const esp_err_t err = esp_wifi_init(&cfg);
        if (err != ESP_OK) {
            std::printf("logger: wifi init failed: %s\n", esp_err_to_name(err));
            return false;
        }
        s_wifi_initialized = true;
    }

    if (!s_events_registered) {
        if (esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiEventHandler, nullptr) != ESP_OK) {
            std::printf("logger: wifi event handler register failed\n");
            return false;
        }
        if (esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &IpEventHandler, nullptr) != ESP_OK) {
            std::printf("logger: ip event handler register failed\n");
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
    if (!InitWifiCore()) {
        return false;
    }

    if (std::strlen(CONFIG_LOGGER_WIFI_SSID) == 0U) {
        std::printf("logger: wifi ssid not set\n");
        return false;
    }

    wifi_config_t wifi_cfg{};
    std::strncpy(reinterpret_cast<char*>(wifi_cfg.sta.ssid),
                 CONFIG_LOGGER_WIFI_SSID,
                 sizeof(wifi_cfg.sta.ssid));
    wifi_cfg.sta.ssid[sizeof(wifi_cfg.sta.ssid) - 1U] = '\0';
    std::strncpy(reinterpret_cast<char*>(wifi_cfg.sta.password),
                 CONFIG_LOGGER_WIFI_PASSWORD,
                 sizeof(wifi_cfg.sta.password));
    wifi_cfg.sta.password[sizeof(wifi_cfg.sta.password) - 1U] = '\0';

    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
        std::printf("logger: wifi set mode failed\n");
        return false;
    }
    if (esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg) != ESP_OK) {
        std::printf("logger: wifi set config failed\n");
        return false;
    }

    xEventGroupClearBits(s_wifi_event_group, kWifiConnectedBit | kWifiFailedBit);
    if (esp_wifi_start() != ESP_OK) {
        std::printf("logger: wifi start failed\n");
        return false;
    }

    const EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                                 kWifiConnectedBit | kWifiFailedBit,
                                                 pdTRUE,
                                                 pdFALSE,
                                                 pdMS_TO_TICKS(kWifiConnectTimeoutMs));
    if ((bits & kWifiConnectedBit) == 0U) {
        std::printf("logger: wifi connect timeout\n");
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
        std::printf("logger: ntp sync timeout\n");
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
        std::printf("logger: mqtt client init failed\n");
        return false;
    }

    s_connect_property.session_expiry_interval = 0U;
    s_connect_property.request_problem_info = true;
    if (esp_mqtt5_client_set_connect_property(s_client, &s_connect_property) != ESP_OK) {
        std::printf("logger: mqtt5 connect property set failed\n");
        return false;
    }

    if (esp_mqtt_client_register_event(s_client,
                                       static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID),
                                       MqttEventHandler,
                                       nullptr) != ESP_OK) {
        std::printf("logger: mqtt event handler register failed\n");
        return false;
    }

    return true;
}

bool PublishLines(const char* topic, const CsvLine* lines, std::size_t count) {
    if (topic == nullptr || lines == nullptr) {
        return false;
    }

    if (!ConnectWifi()) {
        return false;
    }
    SyncTime();

    if (!EnsureMqttClient()) {
        return false;
    }

    xEventGroupClearBits(s_mqtt_event_group, kMqttConnectedBit);
    if (esp_mqtt_client_start(s_client) != ESP_OK) {
        std::printf("logger: mqtt start failed\n");
        return false;
    }

    const EventBits_t bits = xEventGroupWaitBits(s_mqtt_event_group,
                                                 kMqttConnectedBit,
                                                 pdTRUE,
                                                 pdFALSE,
                                                 pdMS_TO_TICKS(kMqttConnectTimeoutMs));
    if ((bits & kMqttConnectedBit) == 0U) {
        std::printf("logger: mqtt connect timeout\n");
        esp_mqtt_client_stop(s_client);
        return false;
    }

    bool ok = true;
    for (std::size_t i = 0U; i < count; ++i) {
        std::size_t payload_len = lines[i].length;
        if (payload_len > 0U && lines[i].buffer[payload_len - 1U] == '\n') {
            --payload_len;
        }

        s_publish_property.payload_format_indicator = true;
        s_publish_property.content_type = "text/csv";
        if (esp_mqtt5_client_set_publish_property(s_client, &s_publish_property) != ESP_OK) {
            std::printf("logger: mqtt5 publish property set failed\n");
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
            std::printf("logger: mqtt publish failed\n");
            ok = false;
        }
    }

    esp_mqtt_client_stop(s_client);
    return ok;
}

} // namespace

bool Init() noexcept {
    return InitWifiCore();
}

bool PublishParameters(const CsvLine* lines, std::size_t count) noexcept {
    if (count == 0U) {
        return true;
    }

    const bool ok = PublishLines(CONFIG_LOGGER_MQTT_TOPIC_PARAMETERS, lines, count);
    esp_wifi_disconnect();
    esp_wifi_stop();
    return ok;
}

bool PublishFailure(const CsvLine& line) noexcept {
    const bool ok = PublishLines(CONFIG_LOGGER_MQTT_TOPIC_FAILURES, &line, 1U);
    esp_wifi_disconnect();
    esp_wifi_stop();
    return ok;
}

} // namespace logger::mqtt
