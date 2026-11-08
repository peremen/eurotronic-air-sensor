cmake_minimum_required(VERSION 3.18)
include(pico_sdk_import.cmake)
project(aq_sniff_logger C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

# SD/FatFs library: -DFATFS_SD_PATH=/path/to/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico for an offline copy,
# otherwise it is downloaded (pinned to a tested commit).
if(FATFS_SD_PATH)
    add_subdirectory(${FATFS_SD_PATH}/src fatfs_build)
else()
    include(FetchContent)
    FetchContent_Declare(fatfs_sd
        GIT_REPOSITORY https://github.com/carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico.git
        GIT_TAG        d5e453404cdbfaa55ab30d285b6ab0b730e84a05
        SOURCE_SUBDIR  src)
    FetchContent_MakeAvailable(fatfs_sd)
endif()

add_executable(aq_sniff_logger main.c decoder.c interp.c cycle.c clock.c record.c sdlog.c hw_config.c logic.c)
pico_generate_pio_header(aq_sniff_logger ${CMAKE_CURRENT_LIST_DIR}/i2c_sampler.pio)
add_compile_definitions(USE_PRINTF)
target_link_libraries(aq_sniff_logger pico_stdlib pico_multicore hardware_pio hardware_dma hardware_irq hardware_clocks
                      no-OS-FatFS-SD-SDIO-SPI-RPi-Pico)
pico_enable_stdio_usb(aq_sniff_logger 1)
pico_enable_stdio_uart(aq_sniff_logger 0)
pico_add_extra_outputs(aq_sniff_logger)
