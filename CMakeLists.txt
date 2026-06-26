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

# Register project components and source file structures
idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver nvs_flash esp_log esp_hw_support
)

# Enforce hardware-accelerated RISC-V single-precision floats
# and aggressive loops unrolling optimization compilation profiles
target_compile_options(${COMPONENT_LIB} PRIVATE
    "-O3"
    "-march=rv32imafc"
    "-mabi=ilp32f"
    "-funroll-loops"
    "-fomit-frame-pointer"
    "-ffast-math"
)

# Pass floating point math extensions directly down into the linker flags
target_link_options(${COMPONENT_LIB} PRIVATE
    "-march=rv32imafc"
    "-mabi=ilp32f"
    "-Wl,--gc-sections"
)
