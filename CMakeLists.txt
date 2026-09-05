# Pico_PIO_Encoder as a Pico SDK library.
#
# Usage from your project's CMakeLists.txt:
#   add_subdirectory(path/to/Pico_PIO_Encoder)
#   target_link_libraries(your_app pico_pio_encoder)
#
# The same repository doubles as an Arduino library (library.properties +
# src/), so one release serves both build systems.

cmake_minimum_required(VERSION 3.13)

if (NOT TARGET pico_pio_encoder)
  add_library(pico_pio_encoder INTERFACE)

  target_sources(pico_pio_encoder INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/Pico_PIO_Encoder.cpp
  )

  target_include_directories(pico_pio_encoder INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src
  )

  target_link_libraries(pico_pio_encoder INTERFACE
    hardware_pio
    hardware_clocks
    hardware_gpio
    hardware_sync
    hardware_timer
    pico_sync
  )
endif()
