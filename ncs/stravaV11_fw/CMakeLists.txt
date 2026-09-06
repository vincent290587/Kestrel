cmake_minimum_required(VERSION 3.20.0)

# sdk-ant is a license-gated vendor SDK, not in our west manifest -- see
# stravaV11_ant/CMakeLists.txt and CLAUDE.md for why ZEPHYR_EXTRA_MODULES
# rather than a manifest import.
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../ant)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(stravav11_fw)

target_include_directories(app PRIVATE lib)

target_sources(app PRIVATE
  src/main.c
  src/ble_demo.c
  src/ant_demo.c
  lib/bt_cp_client.c
)
