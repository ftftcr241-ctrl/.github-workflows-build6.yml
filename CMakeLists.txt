cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2)   # RP2350

# Use the import script shipped with the SDK (no local copy needed).
# PICO_SDK_PATH can come from -DPICO_SDK_PATH=... or from the environment.
if(NOT PICO_SDK_PATH AND DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif()
if(NOT PICO_SDK_PATH)
    message(FATAL_ERROR "PICO_SDK_PATH is not set")
endif()
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(usb_i2s_dac C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(usb_i2s_dac
    main.c
    usb_descriptors.c
)

pico_generate_pio_header(usb_i2s_dac ${CMAKE_CURRENT_LIST_DIR}/i2s_out.pio)
target_include_directories(usb_i2s_dac PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(usb_i2s_dac
    pico_stdlib
    pico_unique_id
    hardware_pio
    hardware_dma
    hardware_clocks
    tinyusb_device
    tinyusb_board
)

pico_add_extra_outputs(usb_i2s_dac)
