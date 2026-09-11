# SPDX-License-Identifier: Apache-2.0

# Phase 11: custom stravaV11 PCB, flashed via a standalone probe over SWD (no
# on-board debugger like the DK has). Both runners below are registered
# since the probe actually in use here is an Arm DAPLink/CMSIS-DAP unit
# (confirmed via `pyocd list`/`nrfutil device list`, 2026-09-11) -- NOT a
# real SEGGER J-Link, which JLinkExe needs to talk to it and this project's
# JLinkExe is separately documented (CLAUDE.md) as unreliable on this
# machine anyway. pyocd is the one that actually works with this probe and
# is now the default flasher/debugger (board_set_flasher_ifnset() only sets
# it the first time it's called, so including pyocd.board.cmake before
# jlink.board.cmake below makes pyocd win unless -r/--runner overrides it);
# jlink stays registered in case a real J-Link probe is ever swapped in.
#
#   west flash --build-dir <dir>                          # pyocd, this probe
#   west flash --build-dir <dir> -- --dev-id <serial>      # pyocd, pick a probe
#   west flash --build-dir <dir> --runner jlink -- --id <serial-number>
board_runner_args(pyocd "--target=nrf52840")
board_runner_args(jlink "--device=nRF52840_xxAA" "--speed=4000")
include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
