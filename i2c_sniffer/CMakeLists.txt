cmake_minimum_required(VERSION 3.13)

# copy pico_sdk_import.cmake from $PICO_SDK_PATH/external/ next to this file first
include(pico_sdk_import.cmake)

project(i2c_sniffer C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(i2c_sniffer main.c decoder.c interp.c)
pico_generate_pio_header(i2c_sniffer ${CMAKE_CURRENT_LIST_DIR}/i2c_sampler.pio)

target_link_libraries(i2c_sniffer pico_stdlib hardware_pio hardware_dma hardware_irq hardware_clocks)

# optional tuning, e.g. target_compile_definitions(i2c_sniffer PRIVATE SAMPLE_RATE_HZ=4000000 PIN_SDA=6)

pico_enable_stdio_usb(i2c_sniffer 1)
pico_enable_stdio_uart(i2c_sniffer 0)
pico_add_extra_outputs(i2c_sniffer)
