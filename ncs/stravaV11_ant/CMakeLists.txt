cmake_minimum_required(VERSION 3.20.0)

# sdk-ant is a proper Zephyr module (has zephyr/module.yml) but isn't in our
# west manifest (it's a downloaded, license-gated SDK -- see CLAUDE.md).
# ZEPHYR_EXTRA_MODULES pulls it in without touching the manifest/NCS version
# pin. Its own west.yml (used only when IT is the manifest root) pins
# sdk-nrf v3.2.4; we're on v3.3.4 -- this app exists to find out whether
# that gap actually matters.
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../ant)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(stravav11_ant)

target_sources(app PRIVATE src/main.c)
