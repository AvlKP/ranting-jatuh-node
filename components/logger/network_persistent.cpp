/// @file network_persistent.cpp
/// @brief Persistent WiFi strategy for dashboard mode.
/// @details WiFi stays connected continuously with auto-reconnect on disconnection.
/// EnsureConnected() checks the event group bit — returns quickly when connected.
/// ReleaseConnection() is a no-op.
/// @ingroup logger

#include "network_strategy.hpp"

#include <cstdint>
#include <cstring>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_eap_client.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "sdkconfig.h"

namespace logger::network {

namespace {

constexpr EventBits_t kWifiConnectedBit = BIT0;
constexpr EventBits_t kWifiFailedBit = BIT1;
constexpr std::uint32_t kWifiConnectTimeoutMs = 20000U;

static const char* kTag = "NET_PERSIST";

StaticEventGroup_t s_wifi_event_group_buffer{};
EventGroupHandle_t s_wifi_event_group = nullptr;

bool s_netif_initialized = false;
bool s_wifi_initialized = false;
bool s_events_registered = false;
bool s_nvs_initialized = false;
std::uint8_t s_last_disconnect_reason = 0U;

const char* WifiReasonString(std::uint8_t reason) {
    switch (reason) {
        case WIFI_REASON_AUTH_FAIL:           return "auth_fail";
        case WIFI_REASON_NO_AP_FOUND:         return "no_ap";
        case WIFI_REASON_ASSOC_FAIL:          return "assoc_fail";
        case WIFI_REASON_HANDSHAKE_TIMEOUT:   return "handshake_timeout";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4way_timeout";
        case WIFI_REASON_BEACON_TIMEOUT:      return "beacon_timeout";
        case WIFI_REASON_MIC_FAILURE:         return "mic_failure";
        case WIFI_REASON_AUTH_EXPIRE:         return "auth_expire";
        case WIFI_REASON_AUTH_LEAVE:          return "auth_leave";
        case WIFI_REASON_ASSOC_EXPIRE:        return "assoc_expire";
        case WIFI_REASON_ASSOC_LEAVE:         return "assoc_leave";
        case WIFI_REASON_NOT_AUTHED:          return "not_authed";
        case WIFI_REASON_NOT_ASSOCED:         return "not_assoc";
        default:                              return "unknown";
    }
}

void WifiEventHandler(void*,
                      esp_event_base_t event_base,
                      int32_t event_id,
                      void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (event_data != nullptr) {
            const auto* disconnected =
                static_cast<wifi_event_sta_disconnected_t*>(event_data);
            s_last_disconnect_reason = disconnected->reason;
            ESP_LOGW(kTag,
                     "WiFi disconnected: reason=%u (%s)",
                     static_cast<std::uint32_t>(disconnected->reason),
                     WifiReasonString(disconnected->reason));
        } else {
            s_last_disconnect_reason = 0U;
            ESP_LOGW(kTag, "WiFi disconnected: reason=unknown");
        }
        if (s_wifi_event_group != nullptr) {
            xEventGroupSetBits(s_wifi_event_group, kWifiFailedBit);
            xEventGroupClearBits(s_wifi_event_group, kWifiConnectedBit);
        }
        // Persistent mode: auto-reconnect
        esp_wifi_connect();
    }
}

void IpEventHandler(void*,
                    esp_event_base_t event_base,
                    int32_t event_id,
                    void* event_data) {
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        if (s_wifi_event_group != nullptr) {
            xEventGroupClearBits(s_wifi_event_group, kWifiFailedBit);
            xEventGroupSetBits(s_wifi_event_group, kWifiConnectedBit);
        }
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

    return s_wifi_event_group != nullptr;
}

} // namespace

bool Init() noexcept {
    return InitWifiCore();
}

bool EnsureConnected() noexcept {
    if (IsConnected()) {
        return true;
    }

    if (!InitWifiCore()) {
        return false;
    }

    if (std::strlen(CONFIG_LOGGER_WIFI_SSID) == 0U) {
        ESP_LOGE(kTag, "WiFi SSID not set");
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
        return false;
    }
    if (esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg) != ESP_OK) {
        ESP_LOGE(kTag, "WiFi set config failed");
        return false;
    }

#if CONFIG_LOGGER_WIFI_ENT_ENABLE
    {
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
    }
#endif

    xEventGroupClearBits(s_wifi_event_group, kWifiConnectedBit | kWifiFailedBit);
    s_last_disconnect_reason = 0U;

    if (esp_wifi_start() != ESP_OK) {
        ESP_LOGE(kTag, "WiFi start failed");
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
        return false;
    }

    return true;
}

void ReleaseConnection() noexcept {
    // Persistent mode: WiFi stays connected
}

bool IsConnected() noexcept {
    if (s_wifi_event_group == nullptr) {
        return false;
    }
    const EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
    return (bits & kWifiConnectedBit) != 0U;
}

} // namespace logger::network
