idf_component_register(SRCS "test_monitor_modal.cpp" "test_monitor_algorithms.cpp" "test_logger_formatting.cpp" "test_outbox_network_runtime.cpp" "test_network_wifi_reason.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES "monitor" "logger" "unity")
