if(TEST_COMPONENTS)
    idf_component_register(SRCS "test_main.cpp"
                        INCLUDE_DIRS "."
                        PRIV_REQUIRES "unity")
elseif(CONFIG_APP_BUILD_RAW_LOGGER)
    set(main_srcs "raw_logger_main.cpp")
    set(main_reqs "lsm6ds3" "sdmmc" "fatfs" "driver" "log" "nvs_flash")
    idf_component_register(SRCS ${main_srcs}
                        INCLUDE_DIRS "."
                        REQUIRES ${main_reqs})
else()
    set(main_srcs "verify.cpp" "main.cpp")
    set(main_reqs "monitor" "logger" "sdmmc" "fatfs" "driver" "log" "dashboard" "esp_event")
    idf_component_register(SRCS ${main_srcs}
                        INCLUDE_DIRS "."
                        REQUIRES ${main_reqs})
endif()
