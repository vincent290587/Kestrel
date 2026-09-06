cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(stravav11_fw)

target_include_directories(app PRIVATE lib)

target_sources(app PRIVATE
  src/main.c
  src/ble_demo.c
  lib/bt_cp_client.c
)
