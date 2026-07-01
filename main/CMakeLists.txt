idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver
             esp_driver_gpio
             esp_driver_i2c
             esp_driver_i2s
             esp_driver_spi
             nvs_flash
             log
             esp_hw_support
)

# Enforce hardware-accelerated RISC-V single-precision floats
# and aggressive loops unrolling optimization compilation profiles.
target_compile_options(${COMPONENT_LIB} PRIVATE
    "-O3"
    "-march=rv32imafc"
    "-mabi=ilp32f"
    "-funroll-loops"
    "-fomit-frame-pointer"
    "-ffast-math"
)

# Pass floating point math extensions directly down into the linker flags.
target_link_options(${COMPONENT_LIB} PRIVATE
    "-march=rv32imafc"
    "-mabi=ilp32f"
    "-Wl,--gc-sections"
)
