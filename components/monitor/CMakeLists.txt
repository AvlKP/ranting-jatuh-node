idf_component_register(SRCS "monitor.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES "lsm6ds3" "filter" "esp-dsp" "esp_timer" "driver" "esp_adc" "log")
