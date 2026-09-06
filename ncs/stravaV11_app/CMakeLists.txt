cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(stravav11_logic_port)

# Reuse stravaV10's existing TDD (host-build) abstraction layer for the
# wrapper headers (segger_wrapper.h, math_wrapper.h, assert_wrapper.h,
# task_manager_wrapper.h): they already compile to hardware-free stubs
# under -DTDD. This is a bridge, not the end state -- a later pass should
# replace these with native Zephyr logging/kernel calls once the driver
# phases land.
add_definitions(-DTDD)

set(LIB_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/lib)
set(ADAPTERS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/adapters)

target_include_directories(app PRIVATE
  ${LIB_ROOT}
  ${LIB_ROOT}/TDD
  ${LIB_ROOT}/TDD/nordic
  ${LIB_ROOT}/TDD/scheduler
  ${LIB_ROOT}/source
  ${LIB_ROOT}/source/routes
  ${LIB_ROOT}/source/model
  ${LIB_ROOT}/source/display
  ${LIB_ROOT}/source/parsers
  ${LIB_ROOT}/source/sensors
  ${LIB_ROOT}/source/maps
  ${LIB_ROOT}/libraries/utils
  ${LIB_ROOT}/libraries/kalman
  ${LIB_ROOT}/libraries/VParser
  ${LIB_ROOT}/libraries/komoot
  ${LIB_ROOT}/libraries/filters
  ${LIB_ROOT}/libraries/TinyGPSPlus
  ${LIB_ROOT}/libraries/AdafruitGFX
  ${LIB_ROOT}/libraries/AdafruitGFX/Fonts
  ${LIB_ROOT}/source/vue
  ${ADAPTERS_ROOT}
)

target_sources(app PRIVATE
  ${LIB_ROOT}/source/g_structs.c
  ${LIB_ROOT}/source/routes/Points.cpp
  ${LIB_ROOT}/source/routes/Vecteur.cpp
  ${LIB_ROOT}/source/routes/ListePoints.cpp
  ${LIB_ROOT}/source/routes/Segment.cpp
  ${LIB_ROOT}/source/routes/Parcours.cpp
  ${LIB_ROOT}/source/model/PowerZone.cpp
  ${LIB_ROOT}/source/model/RRZone.cpp
  ${LIB_ROOT}/source/model/SufferScore.cpp
  ${LIB_ROOT}/source/model/UserSettings.cpp
  ${LIB_ROOT}/source/model/Locator.cpp
  ${LIB_ROOT}/source/display/SegmentManager.cpp
  ${LIB_ROOT}/source/display/Zoom.cpp
  ${LIB_ROOT}/source/maps/map_tile.c
  ${LIB_ROOT}/source/maps/map_render.cpp
  ${LIB_ROOT}/libraries/utils/utils.c
  ${LIB_ROOT}/libraries/utils/WString.cpp
  ${LIB_ROOT}/libraries/kalman/kalman_ext.cpp
  ${LIB_ROOT}/libraries/kalman/UDMatrix.cpp
  ${LIB_ROOT}/libraries/VParser/VParser.cpp
  ${LIB_ROOT}/libraries/komoot/komoot_nav.c
  ${LIB_ROOT}/libraries/filters/order1_filter_tdd.c
  ${LIB_ROOT}/libraries/TinyGPSPlus/TinyGPS++.cpp
  ${LIB_ROOT}/libraries/AdafruitGFX/Adafruit_GFX.cpp
  ${LIB_ROOT}/libraries/AdafruitGFX/Print.cpp
  ${LIB_ROOT}/source/vue/ZephyrGFX.cpp
  ${ADAPTERS_ROOT}/fram_stub.c
  ${ADAPTERS_ROOT}/gps_mgmt_stub.cpp
  ${ADAPTERS_ROOT}/millis_zephyr.c
  src/notifications_stub.c
  src/globals.cpp
  src/main.cpp
)
