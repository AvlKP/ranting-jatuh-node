set(SRCS "network_task.cpp" "outbox.cpp" "logger.cpp" "logger_storage.cpp")

if(CONFIG_DASHBOARD_ENABLE)
    list(APPEND SRCS "network_persistent.cpp")
else()
    list(APPEND SRCS "network_on_demand.cpp")
endif()
list(APPEND SRCS "logger_mqtt.cpp")

idf_component_register(SRCS ${SRCS}
                    INCLUDE_DIRS "include"
                    REQUIRES "monitor" "nvs_flash" "esp_wifi" "esp_event"
                             "esp_netif" "mqtt" "esp_timer" "log" "wpa_supplicant")
