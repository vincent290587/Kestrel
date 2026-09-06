cmake_minimum_required(VERSION 3.20.0)

# sdk-ant is a license-gated vendor SDK, not in our west manifest -- see
# stravaV11_ant/CMakeLists.txt and CLAUDE.md for why ZEPHYR_EXTRA_MODULES
# rather than a manifest import.
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../ant)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(stravav11_fw)

# Phase 7: AdafruitGFX/WString reuse the same -DTDD bridge as stravaV11_app
# (see that app's CMakeLists.txt) -- their #ifndef TDD branches use
# AVR-only builtins (__isnanf etc.) that don't exist on this toolchain.
add_definitions(-DTDD)

target_include_directories(app PRIVATE
  lib
  lib/TDD
  lib/TDD/nordic
  lib/TDD/scheduler
  lib/libraries/AdafruitGFX
  lib/libraries/AdafruitGFX/Fonts
  lib/libraries/utils
  lib/source/vue
)

target_sources(app PRIVATE
  src/main.c
  src/ble_demo.c
  src/ant_demo.c
  src/gfx_demo.cpp
  src/task_demo.c
  src/power_demo.c
  src/poll_demo.c
  lib/bt_cp_client.c
  lib/libraries/AdafruitGFX/Adafruit_GFX.cpp
  lib/libraries/AdafruitGFX/Print.cpp
  lib/libraries/utils/WString.cpp
  lib/source/vue/ZephyrGFX.cpp
)
