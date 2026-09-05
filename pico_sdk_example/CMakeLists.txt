# Standalone Pico SDK example for Pico_PIO_Encoder.
#
# Build (with PICO_SDK_PATH set):
#   cmake -S . -B build -DPICO_BOARD=pico   # or pico2 for RP2350
#   cmake --build build

cmake_minimum_required(VERSION 3.13)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pico_pio_encoder_example C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_subdirectory(.. pico_pio_encoder)

add_executable(position_speed main.cpp)
target_link_libraries(position_speed pico_stdlib pico_pio_encoder)

pico_enable_stdio_usb(position_speed 1)
pico_add_extra_outputs(position_speed)

add_executable(nonconsecutive nonconsecutive.cpp)
target_link_libraries(nonconsecutive pico_stdlib pico_pio_encoder)
pico_enable_stdio_usb(nonconsecutive 1)
pico_add_extra_outputs(nonconsecutive)
