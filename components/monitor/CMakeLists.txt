idf_component_register(SRCS "monitor.cpp"
                           "disturbance_detector.cpp"
                           "ae_detector.cpp"
                           "modal_analyzer.cpp"
                           "monitor_publisher.cpp"
                           "monitor_task.cpp"
                           "sample_store.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES "lsm6ds3" "filter" "esp-dsp" "esp_timer" "driver" "esp_adc" "log" "esp_event" "freertos" "runtime" "nvs_flash")
