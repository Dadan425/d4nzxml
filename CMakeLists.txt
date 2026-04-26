cmake_minimum_required(VERSION 3.13)

# Pull in Pico SDK (harus set PICO_SDK_PATH dulu)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pico_syscon_reader C CXX ASM)

set(CMAKE_C_STANDARD   11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(pico_syscon_reader
    src/main.c
)

# Nama output file
set_target_properties(pico_syscon_reader PROPERTIES OUTPUT_NAME "JGC_Syscon_Reader")

# Library yang dipakai
target_link_libraries(pico_syscon_reader
    pico_stdlib
    hardware_uart
    hardware_gpio
    hardware_timer
    pico_stdio_usb     # USB CDC (muncul sebagai COM port)
)

# Disable UART stdio (kita pakai USB)
pico_enable_stdio_uart(pico_syscon_reader 0)
pico_enable_stdio_usb (pico_syscon_reader 1)

# Generate .uf2, .hex, .bin, .map
pico_add_extra_outputs(pico_syscon_reader)
