idf_component_register(SRCS "logger.cpp" "logger_storage.cpp" "logger_mqtt.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES "monitor" "board" "nvs_flash" "esp_wifi" "esp_event"
                             "esp_netif" "mqtt" "sdmmc" "fatfs" "driver" "esp_timer")
