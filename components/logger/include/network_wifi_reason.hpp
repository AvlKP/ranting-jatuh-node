/// @file network_wifi_reason.hpp
/// @brief Pure helpers for WiFi disconnect reason classification.
/// @details Extracted from on-demand strategy to enable unit testing.
/// @ingroup logger

#pragma once

#include <cstdint>

#include "esp_wifi.h"

namespace logger::network {

inline const char* WifiReasonString(std::uint8_t reason) {
    switch (reason) {
        case WIFI_REASON_AUTH_FAIL:              return "auth_fail";
        case WIFI_REASON_NO_AP_FOUND:            return "no_ap";
        case WIFI_REASON_ASSOC_FAIL:             return "assoc_fail";
        case WIFI_REASON_HANDSHAKE_TIMEOUT:      return "handshake_timeout";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4way_timeout";
        case WIFI_REASON_BEACON_TIMEOUT:         return "beacon_timeout";
        case WIFI_REASON_MIC_FAILURE:            return "mic_failure";
        case WIFI_REASON_AUTH_EXPIRE:            return "auth_expire";
        case WIFI_REASON_AUTH_LEAVE:             return "auth_leave";
        case WIFI_REASON_ASSOC_EXPIRE:           return "assoc_expire";
        case WIFI_REASON_ASSOC_LEAVE:            return "assoc_leave";
        case WIFI_REASON_NOT_AUTHED:             return "not_authed";
        case WIFI_REASON_NOT_ASSOCED:            return "not_assoc";
        default:                                 return "unknown";
    }
}

inline bool IsReleaseReason(std::uint8_t reason) {
    return reason == WIFI_REASON_ASSOC_LEAVE ||
           reason == WIFI_REASON_AUTH_LEAVE;
}

inline bool IsTransientReason(std::uint8_t reason) {
    switch (reason) {
        case WIFI_REASON_AUTH_EXPIRE:
        case WIFI_REASON_ASSOC_EXPIRE:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_BEACON_TIMEOUT:
        case WIFI_REASON_NO_AP_FOUND:
            return true;
        default:
            return false;
    }
}

} // namespace logger::network
