cmake_minimum_required(VERSION 3.16)

# Branch 1: Building with ESP-IDF for ESP32 target
if(DEFINED ENV{IDF_PATH})
    include($ENV{IDF_PATH}/tools/cmake/project.cmake)
    project(esprit_demo)

# Branch 2: Building natively on PC/Host for unit testing
else()
    project(esprit_pc_demo C)
    set(CMAKE_C_STANDARD 11)

    # Tell CMake where to find the component and main app
    add_subdirectory(components/esprit_driver)
    
    add_executable(app main/main.c)
    target_link_libraries(app PRIVATE esprit_driver)
endif()