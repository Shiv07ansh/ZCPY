cmake_minimum_required(VERSION 3.16)

# Detect if building under ESP-IDF or native Host system
if(ESP_PLATFORM)
    idf_component_register(
        SRCS 
            "src/dma_ringbuf.c"
            "src/targets/target_esp32s3.c"
        INCLUDE_DIRS 
            "include"
        REQUIRES 
            driver
            freertos
            esp_driver_i2s
    )
else()
    # Native host build (e.g., gcc/clang for Linux/macOS unit tests)
    add_library(esprit_driver
        "src/dma_ringbuf.c"
        "src/targets/target_posix.c"
    )
    target_include_directories(esprit_driver PUBLIC "include")
endif()