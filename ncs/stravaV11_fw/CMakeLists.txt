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
  adapters
)

target_sources(app PRIVATE
  src/main.c
  src/ble_demo.c
  src/ant_demo.c
  src/hrm_demo.c
  src/bsc_demo.c
  src/gfx_demo.cpp
  src/gps_demo.cpp
  src/sensor_screen_demo.c
  src/task_demo.c
  src/power_demo.c
  src/poll_demo.c
  src/usbd_init.c
  src/usb_demo.c
  lib/bt_cp_client.c
  lib/libraries/AdafruitGFX/Adafruit_GFX.cpp
  lib/libraries/AdafruitGFX/Print.cpp
  lib/libraries/utils/WString.cpp
  lib/libraries/utils/utils.c
  lib/libraries/TinyGPSPlus/TinyGPS++.cpp
  lib/source/model/Locator.cpp
  lib/source/vue/ZephyrGFX.cpp
  adapters/gps_mgmt_stub.cpp
  adapters/millis_zephyr.c
)
