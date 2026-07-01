# Minimum requirements for modern ESP-IDF build target environments
cmake_minimum_required(VERSION 3.16)

# Force initialization of the primary ESP-IDF compilation scripts
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Define the master software project name identifier
project(ESP32P4_Car_Audio_Processor)

# Verify correct target selection for the dual-core RISC-V architecture
if(NOT "${IDF_TARGET}" STREQUAL "esp32p4")
    message(FATAL_ERROR "Target architecture mismatch! Project requires esp32p4.")
endif()
