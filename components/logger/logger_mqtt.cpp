/// @file logger_mqtt.cpp
/// @brief MQTT utilities: node ID management, topic building, MQTT log buffer.
/// @ingroup logger

#include "logger_internal.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_random.h"
#include "sdkconfig.h"
#include "mqtt_log.hpp"

namespace logger::mqtt {

MqttLogBuffer g_mqtt_log_buffer;

namespace {

static const char* kTag = "LOGGER_MQTT";

constexpr char kTopicBasePrefix[] = "ranting/";
constexpr char kTopicDelimiter[] = "/";
constexpr std::size_t kMaxNodeIdLen = 32U;

static const char* kAdjectives[] = {
    "quiet", "bold",  "swift", "calm", "bright", "dark",  "cold",  "warm",
    "wild",  "tame",  "sharp", "blunt","fast",   "slow",  "deep",  "flat",
    "high",  "low",   "soft",  "hard", "free",   "busy",  "lazy",  "keen",
    "dull",  "pure",  "rare",  "rich", "poor",   "thin",  "thick", "fresh",
    "stale", "young", "old",   "vast", "tiny",   "grand", "plain", "crisp",
    "faint", "mild",  "rough", "sleek","still",   "tense", "rigid", "loose",
    "rapid", "steady","light", "heavy","brave",  "shy",   "proud", "stern",
    "sunny", "gray",  "green", "blue", "gold",   "pale",  "lush",  "dry"
};

static const char* kNouns[] = {
    "pine",  "oak",    "elm",   "ash",   "yew",   "fir",   "birch",  "cedar",
    "maple", "willow", "spruce","beech", "walnut","cherry","apple", "pear",
    "plum",  "fig",    "lime",  "palm",  "olive", "hazel", "holly",  "ivy",
    "fern",  "moss",   "reed",  "rush",  "sage",  "mint",  "thyme",  "basil",
    "clove", "anise",  "cumin", "poppy", "lotus", "lily",  "rose",   "iris",
    "daisy", "heath",  "broom", "gorse", "thorn", "vine",  "herb",   "root",
    "bark",  "leaf",   "twig",  "bud",   "seed",  "cone",  "nut",    "pod",
    "bean",  "corn",   "rice",  "wheat", "oats",  "rye",   "flax"
};

static constexpr std::size_t kAdjectiveCount = sizeof(kAdjectives) / sizeof(kAdjectives[0]);
static constexpr std::size_t kNounCount = sizeof(kNouns) / sizeof(kNouns[0]);

static char s_node_id[kMaxNodeIdLen + 1] = {};
static bool s_node_id_resolved = false;

static char s_topic_buffer[64] = {};
static char s_last_datatype[32] = {};

bool s_nvs_initialized = false;

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

void GenerateNodeId(char* out_buf, std::size_t buf_size) {
    const std::uint32_t adj_idx = esp_random() % kAdjectiveCount;
    const std::uint32_t noun_idx = esp_random() % kNounCount;
    std::snprintf(out_buf, buf_size, "%s-%s", kAdjectives[adj_idx], kNouns[noun_idx]);
}

bool ReadNodeIdFromNvs(char* out_buf, std::size_t buf_size) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("logger", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    std::size_t len = buf_size;
    err = nvs_get_str(handle, "node_id", out_buf, &len);
    nvs_close(handle);

    if (err == ESP_OK && len > 0U) {
        out_buf[buf_size - 1U] = '\0';
        return true;
    }
    return false;
}

bool WriteNodeIdToNvs(const char* id) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("logger", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return false;
    }

    err = nvs_set_str(handle, "node_id", id);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err == ESP_OK;
}

const char* ResolveNodeId() {
    if (s_node_id_resolved) {
        return s_node_id;
    }

    if (std::strlen(CONFIG_LOGGER_NODE_ID) > 0U) {
        std::strncpy(s_node_id, CONFIG_LOGGER_NODE_ID, kMaxNodeIdLen);
        s_node_id[kMaxNodeIdLen] = '\0';
        s_node_id_resolved = true;
        return s_node_id;
    }

    InitNvs();

    if (ReadNodeIdFromNvs(s_node_id, kMaxNodeIdLen + 1)) {
        s_node_id_resolved = true;
        return s_node_id;
    }

    GenerateNodeId(s_node_id, kMaxNodeIdLen + 1);
    WriteNodeIdToNvs(s_node_id);
    s_node_id_resolved = true;
    return s_node_id;
}

const char* BuildTopic(const char* datatype) {
    const char* id = ResolveNodeId();
    std::snprintf(s_topic_buffer, sizeof(s_topic_buffer), "%s%s%s%s",
                  kTopicBasePrefix, id, kTopicDelimiter, datatype);
    return s_topic_buffer;
}

} // namespace

const char* GetNodeId() noexcept {
    return ResolveNodeId();
}

const char* GetTopic(const char* datatype) noexcept {
    if (s_last_datatype[0] != '\0' && std::strcmp(s_last_datatype, datatype) == 0) {
        return s_topic_buffer;
    }
    std::strncpy(s_last_datatype, datatype, sizeof(s_last_datatype) - 1U);
    s_last_datatype[sizeof(s_last_datatype) - 1U] = '\0';
    return BuildTopic(datatype);
}

} // namespace logger::mqtt
