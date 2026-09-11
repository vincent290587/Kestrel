cmake_minimum_required(VERSION 3.20.0)

# Phase 11: the real custom PCB's board definition lives under boards/ in
# this app (boards/vincent/stravav11/) -- add it to BOARD_ROOT so
# `-b stravav11/nrf52840` resolves, alongside the built-in nrf52840dk board
# still used for DK bring-up.
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# sdk-ant is a license-gated vendor SDK, not in our west manifest -- see
# stravaV11_ant/CMakeLists.txt and CLAUDE.md for why ZEPHYR_EXTRA_MODULES
# rather than a manifest import.
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../ant)

# Real ANT+ license key: sdk-ant reads it as the Kconfig string
# CONFIG_ANT_LICENSE_KEY (ant/init/ant_init.c's ant_stack_init() call), not
# from a header define -- so the repo-root ANT_license.h (gitignored,
# credential-like material -- see .gitignore) can't be #included anywhere
# to take effect. Instead, extract the key string from it here and hand it
# to Kconfig via a generated fragment on EXTRA_CONF_FILE, which must be set
# before find_package(Zephyr) below. If the header is missing, prj.conf's
# CONFIG_ANT_EVALUATION_KEY=y fallback applies instead -- so a checkout
# without the user's personal key still builds, just against the public
# evaluation key.
set(ant_license_header ${CMAKE_CURRENT_SOURCE_DIR}/../../ANT_license.h)
if(EXISTS ${ant_license_header})
  file(READ ${ant_license_header} ant_license_header_contents)
  string(REGEX MATCH "ANT_LICENSE_KEY[ \t]+\"([^\"]+)\"" _ "${ant_license_header_contents}")
  if(CMAKE_MATCH_1)
    set(ant_license_conf ${CMAKE_BINARY_DIR}/ant_license_generated.conf)
    file(WRITE ${ant_license_conf} "CONFIG_ANT_LICENSE_KEY=\"${CMAKE_MATCH_1}\"\n")
    set(EXTRA_CONF_FILE ${ant_license_conf})
  else()
    message(WARNING "ANT_license.h found but no ANT_LICENSE_KEY \"...\" define in it -- falling back to the ANT_EVALUATION_KEY default")
  endif()
else()
  message(WARNING "ANT_license.h not found at repo root -- falling back to the ANT_EVALUATION_KEY default (non-commercial use only)")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(stravav11_fw)

# Segments/live-race feature (2026-09-11): live-segment racing was already
# descoped by user decision (2026-09-07, see CLAUDE.md's port-plan
# paragraph) -- SegmentManager.cpp/segMngr never gets populated with real
# data anywhere in this port, so its actual runtime behavior is unaffected
# either way. This option makes that descoping compile-time-real instead of
# just "not pursued yet": OFF drops Segment.cpp/SegmentManager.cpp entirely
# and compiles out every segment-drawing method in VueCRS/VuePRC (see
# STRAVA_SEGMENTS_ENABLED's #ifdef guards there and in vue_globals.cpp) --
# real flash savings when a build is tight on space (see
# pm_static_stravav11_nrf52840.yml's own notes on this board's thin margin),
# not just dead-code-eliminated already by the linker (VueCRS/VuePRC's
# segment methods are reachable via `this->afficheSegment(...)` from other
# member functions of the same class, so --gc-sections can't drop them on
# its own the way it drops a genuinely uncalled free function). Defaults
# OFF (2026-09-11, changed from an initial ON default the same day) --
# this feature is already descoped project-wide, and building it out by
# default is exactly what unblocks the Lezyne feature's flash budget (see
# CLAUDE.md's own note) without needing every build invocation to
# remember the flag. Pass -DSTRAVA_SEGMENTS_ENABLED=ON to build it back in.
option(STRAVA_SEGMENTS_ENABLED "Compile in the segments/live-race feature (Segment geometry, SegmentManager, VueCRS/VuePRC segment screens)" OFF)
if(STRAVA_SEGMENTS_ENABLED)
  add_definitions(-DSTRAVA_SEGMENTS_ENABLED)
endif()

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
  lib/libraries/TinyGPSPlus
  lib/source
  lib/source/model
  lib/source/sensors
  lib/source/vue
  lib/source/maps
  lib/source/display
  lib/source/parsers
  lib/source/routes
  lib/libraries/komoot
  lib/libraries/fitLib
  lib/lezyne
  adapters
)

target_sources(app PRIVATE
  src/main.c
  src/ble_demo.c
  src/ant_demo.c
  src/hrm_demo.c
  src/bsc_demo.c
  src/fec_demo.c
  src/ant_dm_demo.c
  src/stc3100_demo.c
  src/smp_demo.c
  src/gfx_demo.cpp
  src/gps_demo.cpp
  src/sensor_screen_demo.c
  src/task_demo.c
  src/power_demo.c
  src/poll_demo.c
  src/usbd_init.c
  src/usb_demo.c
  src/cmd_console.c
  src/sd_format.c
  src/map_demo.c
  src/map_screen_demo.c
  src/sd_stress_demo.c
  src/notifications_demo.c
  src/settings_demo.cpp
  src/model_glue.c
  src/vue_globals.cpp
  src/vue_demo.cpp
  lib/source/vue/MenuObjects.cpp
  lib/source/vue/Menuable.cpp
  lib/source/vue/Screenutils.cpp
  lib/source/vue/VueDebug.cpp
  lib/source/vue/VueGPS.cpp
  lib/source/vue/VueFEC.cpp
  lib/source/vue/VuePRC.cpp
  lib/source/vue/VueCRS.cpp
  lib/source/vue/Vue.cpp
  lib/libraries/komoot/komoot_nav.c
  lib/source/routes/Points.cpp
  lib/source/routes/Vecteur.cpp
  lib/source/routes/ListePoints.cpp
  lib/source/routes/Parcours.cpp
  src/menu_content.cpp
  src/debug_screen_data.cpp
  lib/source/g_structs.c
  lib/source/model/PowerZone.cpp
  lib/source/model/RRZone.cpp
  lib/source/model/SufferScore.cpp
  lib/bt_cp_client.c
  lib/ant_fec.c
  lib/ant_device_manager.c
  lib/stc3100.c
  lib/source/maps/map_tile.c
  lib/source/maps/map_render.cpp
  lib/source/display/Zoom.cpp
  lib/source/display/notifications.c
  lib/libraries/AdafruitGFX/Adafruit_GFX.cpp
  lib/libraries/AdafruitGFX/Print.cpp
  lib/libraries/utils/WString.cpp
  lib/libraries/utils/utils.c
  lib/libraries/TinyGPSPlus/TinyGPS++.cpp
  lib/source/model/Locator.cpp
  lib/source/model/UserSettings.cpp
  lib/source/vue/ZephyrGFX.cpp
  adapters/gps_mgmt_stub.cpp
  adapters/millis_zephyr.c
  adapters/drv_ws2812.c
  adapters/fram_zephyr.c
  lib/libraries/fitLib/fit.c
  lib/libraries/fitLib/fit_crc.c
  lib/libraries/fitLib/fit_example.c
  src/ride_recorder.c
  src/lezyne_ble.c
  src/lezyne_handler.c
)

if(STRAVA_SEGMENTS_ENABLED)
  target_sources(app PRIVATE
    lib/source/routes/Segment.cpp
    lib/source/display/SegmentManager.cpp
  )
endif()
