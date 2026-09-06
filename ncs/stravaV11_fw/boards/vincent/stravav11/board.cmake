# SPDX-License-Identifier: Apache-2.0

# Phase 11: custom stravaV11 PCB, flashed via a standalone J-Link probe over
# SWD (no on-board debugger like the DK has). Pass the probe's serial number
# at flash time: west flash --build-dir <dir> -- --id <serial-number>
board_runner_args(jlink "--device=nRF52840_xxAA" "--speed=4000")
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
