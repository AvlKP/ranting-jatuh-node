idf_component_register(SRCS "verify.cpp" "main.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES "monitor" "logger" "sdmmc" "fatfs" "driver" "log" "dashboard")
