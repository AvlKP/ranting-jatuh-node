idf_component_register(SRCS "dashboard.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES "esp_http_server" "monitor" "logger" "esp_wifi" "esp_event" "log" "fatfs")
