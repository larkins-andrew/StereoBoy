# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# # == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
# if(WIN32)
#     set(USERHOME $ENV{USERPROFILE})
# else()
#     set(USERHOME $ENV{HOME})
# endif()
# set(sdkVersion 2.2.0)
# set(toolchainVersion 14_2_Rel1)
# set(picotoolVersion 2.2.0)
# set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
# if (EXISTS ${picoVscode})
#     include(${picoVscode})
# endif()
# # ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK
set(PICO_SDK_PATH ${CMAKE_CURRENT_SOURCE_DIR}/pico-sdk)
include(pico_sdk_import.cmake)

project(stereoBoy_RP2350_FW C CXX ASM)

pico_sdk_init()

# ============================
# Source Files
# ============================

set(SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/main.c
    ${CMAKE_CURRENT_LIST_DIR}/src/diskio.c
    ${CMAKE_CURRENT_LIST_DIR}/src/ff.c
    ${CMAKE_CURRENT_LIST_DIR}/src/ffsystem.c
    ${CMAKE_CURRENT_LIST_DIR}/src/ffunicode.c
    ${CMAKE_CURRENT_LIST_DIR}/src/my_spi.c
    ${CMAKE_CURRENT_LIST_DIR}/src/sd_card_spi.c
    ${CMAKE_CURRENT_LIST_DIR}/src/sd_spi.c
    ${CMAKE_CURRENT_LIST_DIR}/src/st7789_lcd.c
)

add_executable(stereoBoy_RP2350_FW ${SOURCES})

pico_set_program_name(stereoBoy_RP2350_FW "stereoBoy_RP2350_FW")
pico_set_program_version(stereoBoy_RP2350_FW "0.1")

# Generate PIO header
pico_generate_pio_header(stereoBoy_RP2350_FW ${CMAKE_CURRENT_LIST_DIR}/src/main.pio)

# stdio config
pico_enable_stdio_uart(stereoBoy_RP2350_FW 0)
pico_enable_stdio_usb(stereoBoy_RP2350_FW 1)

# Link libraries
target_link_libraries(stereoBoy_RP2350_FW
    pico_stdlib
    hardware_pio
    hardware_interp
    hardware_spi
    hardware_i2c
    hardware_dma
    hardware_watchdog
)

# Include paths
target_include_directories(stereoBoy_RP2350_FW PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

# Extra outputs (UF2, bin, etc.)
pico_add_extra_outputs(stereoBoy_RP2350_FW)