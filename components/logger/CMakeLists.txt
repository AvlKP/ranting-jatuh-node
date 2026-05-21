idf_component_register(SRCS "logger.cpp" "logger_storage.cpp" "logger_mqtt.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES "monitor" "nvs_flash" "esp_wifi" "esp_event"
                             "esp_netif" "mqtt" "esp_timer" "log" "wpa_supplicant")
