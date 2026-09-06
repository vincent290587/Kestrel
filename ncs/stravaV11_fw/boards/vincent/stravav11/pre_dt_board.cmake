# SPDX-License-Identifier: Apache-2.0

# Same nRF52840 SoC devicetree address overlaps the DK's pre_dt_board.cmake
# suppresses (power@40000000 & clock@40000000 & bprot@40000000; acl@4001e000
# & flash-controller@4001e000) -- not board-specific, just SoC-inherent.
list(APPEND EXTRA_DTC_FLAGS "-Wno-unique_unit_address_if_enabled")
