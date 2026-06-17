#include <cstdint>

#include "unity.h"

#include "network_wifi_reason.hpp"

namespace {

using logger::network::WifiReasonString;
using logger::network::IsReleaseReason;
using logger::network::IsTransientReason;

TEST_CASE("release reason classifies assoc_leave and auth_leave", "[logger][network][reason]") {
    TEST_ASSERT_TRUE(IsReleaseReason(WIFI_REASON_ASSOC_LEAVE));
    TEST_ASSERT_TRUE(IsReleaseReason(WIFI_REASON_AUTH_LEAVE));
}

TEST_CASE("release reason rejects connect failure codes", "[logger][network][reason]") {
    TEST_ASSERT_FALSE(IsReleaseReason(WIFI_REASON_AUTH_FAIL));
    TEST_ASSERT_FALSE(IsReleaseReason(WIFI_REASON_NO_AP_FOUND));
    TEST_ASSERT_FALSE(IsReleaseReason(WIFI_REASON_HANDSHAKE_TIMEOUT));
    TEST_ASSERT_FALSE(IsReleaseReason(WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT));
    TEST_ASSERT_FALSE(IsReleaseReason(WIFI_REASON_BEACON_TIMEOUT));
    TEST_ASSERT_FALSE(IsReleaseReason(0U));
}

TEST_CASE("reason string maps known codes", "[logger][network][reason]") {
    TEST_ASSERT_EQUAL_STRING("assoc_leave", WifiReasonString(WIFI_REASON_ASSOC_LEAVE));
    TEST_ASSERT_EQUAL_STRING("auth_leave", WifiReasonString(WIFI_REASON_AUTH_LEAVE));
    TEST_ASSERT_EQUAL_STRING("handshake_timeout", WifiReasonString(WIFI_REASON_HANDSHAKE_TIMEOUT));
    TEST_ASSERT_EQUAL_STRING("4way_timeout", WifiReasonString(WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT));
    TEST_ASSERT_EQUAL_STRING("auth_fail", WifiReasonString(WIFI_REASON_AUTH_FAIL));
    TEST_ASSERT_EQUAL_STRING("no_ap", WifiReasonString(WIFI_REASON_NO_AP_FOUND));
}

TEST_CASE("reason string returns unknown for unmapped codes", "[logger][network][reason]") {
    TEST_ASSERT_EQUAL_STRING("unknown", WifiReasonString(255U));
    TEST_ASSERT_EQUAL_STRING("unknown", WifiReasonString(0U));
}

TEST_CASE("transient reason classifies retryable failures", "[logger][network][reason]") {
    TEST_ASSERT_TRUE(IsTransientReason(WIFI_REASON_AUTH_EXPIRE));
    TEST_ASSERT_TRUE(IsTransientReason(WIFI_REASON_ASSOC_EXPIRE));
    TEST_ASSERT_TRUE(IsTransientReason(WIFI_REASON_HANDSHAKE_TIMEOUT));
    TEST_ASSERT_TRUE(IsTransientReason(WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT));
    TEST_ASSERT_TRUE(IsTransientReason(WIFI_REASON_BEACON_TIMEOUT));
    TEST_ASSERT_TRUE(IsTransientReason(WIFI_REASON_NO_AP_FOUND));
}

TEST_CASE("transient reason rejects fatal and release codes", "[logger][network][reason]") {
    TEST_ASSERT_FALSE(IsTransientReason(WIFI_REASON_AUTH_FAIL));
    TEST_ASSERT_FALSE(IsTransientReason(WIFI_REASON_ASSOC_LEAVE));
    TEST_ASSERT_FALSE(IsTransientReason(WIFI_REASON_AUTH_LEAVE));
    TEST_ASSERT_FALSE(IsTransientReason(WIFI_REASON_MIC_FAILURE));
    TEST_ASSERT_FALSE(IsTransientReason(0U));
}

} // namespace
