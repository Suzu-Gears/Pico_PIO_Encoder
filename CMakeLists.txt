# SubstepEncoder as a Pico SDK library.
#
# Usage from your project's CMakeLists.txt:
#   add_subdirectory(path/to/SubstepEncoder)
#   target_link_libraries(your_app substep_encoder)
#
# The same repository doubles as an Arduino library (library.properties +
# src/), so one release serves both build systems.

cmake_minimum_required(VERSION 3.13)

if (NOT TARGET substep_encoder)
  add_library(substep_encoder INTERFACE)

  target_sources(substep_encoder INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/SubstepEncoder.cpp
  )

  target_include_directories(substep_encoder INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src
  )

  target_link_libraries(substep_encoder INTERFACE
    hardware_pio
    hardware_clocks
    hardware_gpio
    hardware_sync
    hardware_timer
  )
endif()
