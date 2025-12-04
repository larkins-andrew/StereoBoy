cmake_minimum_required(VERSION 3.13)

project(stereoBoy_RP2350_FW_project C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# --- Load Pico SDK ---
set(PICO_SDK_PATH ${CMAKE_CURRENT_SOURCE_DIR}/pico-sdk)
include(pico_sdk_import.cmake)
pico_sdk_init()

# --- Create the INTERFACE library (from their original CMakeLists) ---
add_library(stereoBoy_RP2350_FW INTERFACE)

# Ensure C++ features are enabled (required because we add a .cpp file)
target_compile_features(stereoBoy_RP2350_FW INTERFACE cxx_std_17)

pico_generate_pio_header(
    stereoBoy_RP2350_FW
    ${CMAKE_CURRENT_LIST_DIR}/src/sd_driver/SDIO/rp2040_sdio.pio
)

target_compile_definitions(stereoBoy_RP2350_FW INTERFACE
    PICO_MAX_SHARED_IRQ_HANDLERS=8u
)

target_sources(stereoBoy_RP2350_FW INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/ff15/source/ff.c
    ${CMAKE_CURRENT_LIST_DIR}/src/ff15/source/ffsystem.c
    ${CMAKE_CURRENT_LIST_DIR}/src/ff15/source/ffunicode.c
    ${CMAKE_CURRENT_LIST_DIR}/src/sd_driver/dma_interrupts.c
    ${CMAKE_CURRENT_LIST_DIR}/src/sd_driver/sd_card.c
    ${CMAKE_CURRENT_LIST_DIR}/src/sd_driver/sd_timeouts.c
    ${CMAKE_CURRENT_LIST_DIR}/src/sd_driver/SDIO/rp2040_sdio.c
    ${CMAKE_CURRENT_LIST_DIR}/src/sd_driver/SDIO/sd_card_sdio.c
    ${CMAKE_CURRENT_LIST_DIR}/src/sd_driver/SPI/my_spi.c
    ${CMAKE_CURRENT_LIST_DIR}/src/sd_driver/SPI/sd_card_spi.c
    ${CMAKE_CURRENT_LIST_DIR}/src/sd_driver/SPI/sd_spi.c

    ${CMAKE_CURRENT_LIST_DIR}/src/FatFsSd.cpp

    ${CMAKE_CURRENT_LIST_DIR}/src/crash.c
    ${CMAKE_CURRENT_LIST_DIR}/src/crc.c
    ${CMAKE_CURRENT_LIST_DIR}/src/f_util.c
    ${CMAKE_CURRENT_LIST_DIR}/src/ff_stdio.c
    ${CMAKE_CURRENT_LIST_DIR}/src/file_stream.c
    ${CMAKE_CURRENT_LIST_DIR}/src/glue.c
    ${CMAKE_CURRENT_LIST_DIR}/src/my_debug.c
    ${CMAKE_CURRENT_LIST_DIR}/src/my_rtc.c
    ${CMAKE_CURRENT_LIST_DIR}/src/util.c
    ${CMAKE_CURRENT_LIST_DIR}/src/hw_config.c
)

target_include_directories(stereoBoy_RP2350_FW INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ff15/source
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sd_driver
    ${CMAKE_CURRENT_SOURCE_DIR}/src/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(PICO_RISCV)
    set(HWDEP_LIBS hardware_watchdog)
else()
    set(HWDEP_LIBS cmsis_core)
endif()

target_link_libraries(stereoBoy_RP2350_FW INTERFACE
    hardware_dma
    hardware_pio
    hardware_spi
    hardware_sync
    pico_aon_timer
    pico_stdlib
    ${HWDEP_LIBS}
)

# --- Your actual firmware executable ---
add_executable(app src/main.c)
set_target_properties(app PROPERTIES OUTPUT_NAME "stereoBoy_RP2350_FW")
set_target_properties(app PROPERTIES SUFFIX ".elf")
target_link_libraries(app stereoBoy_RP2350_FW)
pico_add_extra_outputs(app)
