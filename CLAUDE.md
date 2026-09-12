# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository. It covers current state and what you need to build/flash/work here. Full phase-by-phase porting history — bugs found and their root causes, exact validation logs, byte counts — lives in `docs/PORT_HISTORY.md`; read it when picking this project back up mid-port or investigating why something is built the way it is, but don't assume it needs updating for routine work.

## Repository purpose

This repo is porting `stravaV10` — a bicycle GPS computer firmware for a custom nRF52840 PCB, currently on Nordic's legacy nRF5 SDK v16 + s340 (BLE+ANT) softdevice — to nRF Connect SDK (sdk-nrf) + Zephyr. `stravaV10/` is the original firmware (its own git repo/remote, unrelated history); `ncs/` is the new west workspace for the port.

## Commands

### Zephyr/NCS workspace (`ncs/`) — the active port target

Activate the environment first, in every new shell:
```
source /home/vincent/Github/StravaV11/.venv/bin/activate
cd /home/vincent/Github/StravaV11/ncs
```

Build:
```
west build -p always -b native_sim/native/64 stravaV11_app -d <build-dir>       # logic tier, host sim (see note below on the /native/64 qualifier)
west build -p always -b nrf52840dk/nrf52840 stravaV11_fw -d <build-dir>         # hardware bring-up app, bare DK
west build -p always -b stravav11/nrf52840 stravaV11_fw -d <build-dir> -- -DBOARD_ROOT=<path-to-stravaV11_fw>   # real custom PCB
```
Use `native_sim/native/64`, not plain `native_sim` — the latter defaults to a 32-bit build and this machine's g++14 lacks 32-bit multilib libstdc++ headers.

Flash and read console:
```
west flash --build-dir <build-dir>                                   # DK, via on-board J-Link
west flash --build-dir <build-dir> -- --id <probe-serial-number>      # real PCB, via external probe (needed when >1 probe attached)
# DK console: /dev/ttyACM0, 115200 8N1 (stty -F /dev/ttyACM0 115200 raw -echo)
nrfutil device list                          # confirm a DK is detected
nrfutil device reset --serial-number <sn>    # reset without reflashing
```
**The real custom PCB's console/logging is RTT, not UART** — its one UART pin pair is dedicated to the GPS module. Segger's own `JLinkExe`/`JLinkRTTLogger` are unreliable on this machine (intermittent segfaults, confirmed unrelated to the target); use `pyocd rtt` instead (needs a fake TTY: `script -qec "pyocd rtt ..." /dev/null`). **Never enable `CONFIG_SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL` casually** — it's currently on for bring-up but is a real hazard with short attach/detach captures: it can halt the CPU and has corrupted the SD card. If you touch RTT config, read `docs/PORT_HISTORY.md`'s Phase 11 RTT-tooling section first.

Toolchain: **NCS pinned to v3.2.4** (downgraded from v3.3.4 — this fixed a real, reproducible MPSL/ANT+ crash pairing a live sensor; see `docs/PORT_HISTORY.md` Phase 11. Don't bump this back up without re-checking sdk-ant's compatibility matrix first). Zephyr SDK 0.17.4, `arm-zephyr-eabi` toolchain only (Cortex-M4 target — don't install other archs).

### Legacy firmware (`stravaV10/`) — reference only, not actively built here

Real hardware build requires the nRF5 SDK v16 tree + GCC 6 2017-q2-update ARM toolchain (neither installed here):
```
cd stravaV10/pca10056/s340/armgcc
make                    # needs Makefile.local (gitignored, machine-specific) defining SDK_ROOT
make flash_softdevice   # or: make dfu_softdevice
make flash              # or: make dfu
```

Host-side test/simulation build (business logic + a mocked LCD/GPS/Nordic-API layer, runs on Linux with no target hardware — reuse this when porting logic, since it already proves the code is hardware-decoupled):
```
cd stravaV10 && mkdir build && cd build
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug .. && make
./StravaV10
```
`TDD/LS027simulator.jar` (legacy) or `stravaV11_app/tools/ls027_viewer.py` (current, Python+Pygame — same TCP wire protocol) render what the real LS027 display would show, when the app is built with the `LS027_GUI` env var set. `TDD/GPX_simu*.csv` are canned GPX replay inputs.

## Architecture

### Decisions locked in

- ANT+ is kept (not dropped for BLE-only).
- Business-logic layer validated on `native_sim` before driver/board work; hardware bring-up targets the nRF52840-DK before the custom PCB.
- Out of scope: `AP/` companion-dongle firmware, `ble_lns_c`, `ble_komoot_c`, `zpm/` (PC-side tooling).
- Live-segment racing (`Boucle*.cpp`, `SegmentManager`'s race-pace notification) is descoped — see `STRAVA_SEGMENTS_ENABLED` below, now a real compile-time option defaulting OFF.
- The "Lezyne feature" (BLE impersonation of a real Lezyne GPS computer so the device can talk to Lezyne's own "GPS Ally" phone app) is an active porting target — status below.

### `stravaV10` structure (legacy firmware being ported)

- **Business logic (hardware-agnostic C/C++, proven portable by the TDD host harness)**: `source/Model.*`, `source/model/*` (Attitude, Locator, PowerZone, RRZone, SufferScore, UserSettings), `source/routes/*` (GPX/segment geometry), `source/model/Boucle*` (CRS/FEC/Zwift ride-loop state machines), `libraries/filters`, `libraries/kalman`, `libraries/VParser`, `libraries/komoot/komoot_nav.c`.
- **UI**: `source/vue/*` (menu system, screens per mode) drawn via `libraries/AdafruitGFX` onto the custom `drivers/lcd/ls027.c` Sharp memory LCD driver.
- **Connectivity**: `libraries/ant_profiles/*` (ANT+ FE-C/HRM/cadence-speed/glasses) and `libraries/ble_services/*` (`ble_cp_c`, `ble_lns_c`, `ble_komoot_c` — BLE GATT central profiles), both written against SDK16 APIs that don't exist on Zephyr.
- **Sensors**: `source/sensors/*` — `bme280`/`fxos` have upstream Zephyr drivers; `ms5637`/`STC3100`/`VEML6075`/`fram` needed custom work (see status below — VEML6075 in particular may not even be populated on the real board, see the `project_hardware_and_status_corrections` memory).
- **Storage**: `source/sd/*` (SD/FatFS) and `libraries/SST` (external NOR flash) map onto Zephyr's `disk_access`/`flash`/`fs`.
- **SDK16 glue trashed, not ported**: `task_manager`+wrappers (Zephyr threads/work queues/timers), `drivers/gpio.c`/`i2c.c`/`spi.c`/`uart.c` (devicetree + Zephyr drivers), `hardfault`, `helper.*`/`math_wrapper*`/`assert_wrapper.h`, `dfu/` + old bootloader (MCUboot), `pca10056/`/`hw_test/`/`custom_board_v*.h` (new devicetree board def), `rtt`/`sysview`/`jscope` (Zephyr's native backends).

### `ncs/` workspace structure

Standard NCS layout: `nrf` (manifest repo), `zephyr`, `nrfxlib`, `bootloader` (mcuboot), `modules`, `tools`, plus `ant/` — the proprietary sdk-ant vendor tree, **gitignored, never commit it** (license-gated). Two applications:
- **`stravaV11_app/`** — the native_sim-validated business-logic tier (Model subset, routes, filters/kalman/VParser/komoot, GPS/Locator, ZephyrGFX+map rendering, RISC-V sandbox spike). No hardware dependencies; this is where new hardware-agnostic logic ports should land first.
- **`stravaV11_fw/`** — the hardware bring-up app (DK and the real `stravav11/nrf52840` board). Kept separate from `stravaV11_app` because of board-specific devicetree; the two get merged once both are mature.

### Current port status (see `docs/PORT_HISTORY.md` for full narrative, root causes, and validation evidence)

**Business logic** — done. Ported and validated on both `native_sim` and real hardware: routes, PowerZone/RRZone/SufferScore/UserSettings, filters/kalman/VParser/komoot, `Locator`/TinyGPS++ (real NMEA parsing wired through on the real board, including a GPS simulation feature for indoor testing — `"SIM START"`/`"SIM STOP"` console commands). Not ported: `Model.cpp` (orchestrator), `Boucle*.cpp` (ride-mode controllers), UserSettings' real FRAM persistence (adapter stub returns `false`, always falls back to in-memory defaults).

**Drivers/sensors** — `bme280`, `fxos8700`, FRAM (`mb85rcxx`), QSPI NOR flash (incl. XIP/memory-mapped mode), SD card (raw + FAT/LFN), and `STC3100` (custom driver, real battery data) are all done and validated on real hardware. `ms5637` and `VEML6075` have no custom driver (low priority — `ms5637` isn't the active barometer choice; `VEML6075` may not even be populated on the real board). **The STC3100 also plays a second, boot-critical role**: the board's regulator only stays on while the chip's IO0 pin is actively driven, so `stc3100_power_latch_hold()` runs as the literal first line of `main()` in `stravaV11_fw`, with a periodic 5s re-assertion — do not remove or delay this call.

**Display** — Zephyr's upstream `sharp,ls0xx` driver is reused directly (no custom driver needed). `ZephyrGFX` bridges `Adafruit_GFX` (ported unmodified) onto it. Confirmed correct on real glass by the user ("Looks perfect"). The real board's `cs-gpios` polarity must be `GPIO_ACTIVE_HIGH` (the driver hardcodes `SPI_CS_ACTIVE_HIGH`) — this was a real wiring bug once, fixed. VCOM is hardware-toggled on this board; the `serial-vcom-*` devicetree properties are deliberately absent (not needed, and not present in the pinned v3.2.4 binding anyway).

**Connectivity** — ANT+ (wildcard RX, HRM/BSC/FEC profiles, device-manager search/pairing) and BLE central (`bt_cp_client` for Cycling Power, `bt_hrs_client` for Heart Rate) are both done and validated concurrently on real hardware with real sensors (a Favero Assioma PRO power meter, a real HRM strap on both ANT+ and BLE simultaneously). BSC still uses a hardcoded, non-wildcard device number (deprioritized by user decision, not chased further). `glasses.c` (a custom ANT+ display-glasses protocol) is the one unported ANT+ profile. **ANT+ is now `ANT_ENABLED`-gated at build time, default OFF** (see the Bootloader/DFU flash-budget section below) — pass `-DANT_ENABLED=ON` to get it back for real-hardware ANT+/HRM/BSC/FEC testing.

**Scheduling/power** — `task_manager`/`power_scheduler`/`i2c_scheduler` all replaced with native Zephyr threads/`k_event`/`k_work_delayable`, validated running concurrently on real hardware.

**USB** — CDC-ACM done, validated with real host-side enumeration. Per the `project_hardware_and_status_corrections` memory, **USB MSC is also actually done now** — treat any older note claiming otherwise as stale.

**Bootloader/DFU** — MCUboot boots the app; a full BLE/MCUmgr SMP OTA update cycle (upload → swap → boot) validated end-to-end on real hardware. **Flash budget is tight and real**: the real board uses a static, asymmetric partition layout (`stravaV11_fw/pm_static_stravav11_nrf52840.yml`, primary 124 / secondary 123 sectors as of the 2026-09-11 ride-recording-feature rebalance — check that file's own comments for the current split and the true documented per-slot budget) computed by hand against MCUboot's true `app_max_size()` (which reserves more than a flat linker FLASH-region check shows). **Always check `zephyr.signed.bin`'s exact byte size against that documented budget before trusting a build** — `west build`'s flat "NN% used" summary is a different, more forgiving number and will not catch an overflow; a build that links and signs fine can still fail MCUboot's real swap-move validation *at boot* (spins forever in MCUboot's own fault-hardened halt loop — looks exactly like a hung/crashed board, not an oversized image; confirmed for real 2026-09-11, see `project_stravav11_fw_gotchas` memory). Do not trim existing bring-up/demo code in `main.c` to recover budget (explicit user direction) — use `STRAVA_SEGMENTS_ENABLED=OFF`/`ANT_ENABLED=OFF` (both default OFF) or WRN-level logging instead.

**Flash-saving build options** (both CMake `option()`s, both default OFF, both in `stravaV11_fw/CMakeLists.txt`): passing either flag after `--` on the *initial* `west build` doesn't reach the sysbuild sub-image's CMake cache — re-run `cmake -DFOO=ON <build-dir>/stravaV11_fw` directly against the sub-image dir, then `ninja`, if you need to toggle one on an existing build dir.
- `STRAVA_SEGMENTS_ENABLED` — excludes `Segment.{h,cpp}`/`SegmentManager.{h,cpp}` and segment-drawing code in `VueCRS`/`VuePRC`. Saves ~5.6KB.
- `ANT_ENABLED` (added 2026-09-11) — excludes `ant_demo.c`/`hrm_demo.c`/`bsc_demo.c`/`fec_demo.c`/`ant_dm_demo.c`, `lib/ant_fec.c`, and sdk-ant's own precompiled `libant.a`. Saves **~41KB** (measured: 458KB signed vs ~499KB with ANT+ in). `model_glue.c`/`ride_recorder.c`/`menu_content.cpp` still call into the HRM/BSC/FEC/device-manager API unconditionally as the live-sensor-data bridge — `src/ant_stubs.c` (compiled only when `!ANT_ENABLED`) provides always-"never paired" stub implementations of exactly those functions so the link still succeeds. `lib/ant_device_manager.c` stays unconditionally compiled either way (no dependency on the real ANT+ stack). With ANT+ off, `bt_enable()` alone brings up MPSL, same as any BT-only build.

**Lezyne feature** ("LE GPS 12" BLE identity, talks to Lezyne's real "GPS Ally" app): BLE plumbing + FIT file list/download/delete (`stravaV11_fw/src/lezyne_ble.{h,c}`, `lezyne_handler.{h,c}`) validated on real hardware **against the real GPS Ally app** — it now discovers, connects, lists, and deletes files correctly. Getting there took several real fixes found only by testing against the real app (decompiled `LezyneCycleComputerDevice.txt`/`BluetoothLeService.txt`/`Lezyne_app.md` at the repo root are the reverse-engineering notes):
- The device must advertise a hand-rolled GATT service using the *real* Lezyne UUIDs (`904d0001-2ce9-078d-944d-263fd93d95b2` service, `904d0002`/`904d0003` RX/TX) plus the standard Location and Navigation service (`0x1819`/`0x2A67`) — GPS Ally's own `onServicesDiscovered()` disconnects immediately if either is missing. The original NUS-based transport (this port's *and* legacy stravaV10's own guess) was simply wrong.
- The device's BLE identity address must end in `37:B4` (`isLezyneDevice()`'s own hardcoded address-suffix check) — forced via `bt_id_create()` in `ble_demo.c` before `bt_enable()`.
- FatFS/SD work triggered by a real phone session (file list, and especially the periodic download-chunk read) must run on a dedicated thread with its own generously-sized, isolated stack — doing it on the shared Bluetooth RX thread or the system workqueue caused two real stack-overflow crashes during testing. All of it (RX command processing + the download tick) now runs on one dedicated thread in `lezyne_handler.c`.
- **A real, confirmed bug (found 2026-09-11, fix not yet flashed-and-reverified as of this writing — check `ride_recorder.c` git blame/the `project_lezyne_ble_protocol_correction` memory before assuming this is done)**: FIT `time_created`/`timestamp`/`start_time` fields need the FIT epoch (seconds since 1989-12-31 UTC per `fit_example.h`'s own `FIT_DATE_TIME` comment), not Unix epoch (1970) — `ride_recorder.c` was writing raw Unix timestamps directly into these fields, so every ride's real date decoded ~20 years in the future (confirmed independently with Python's `fitparse` against a real file pulled off the SD card via USB MSC: 2026 decoded as 2046). This is the most likely cause of GPS Ally crashing on every real file *download* (`Ride.fromFitFile(...).save(...)` almost certainly chokes on an implausible future date) — downloading was still untested end-to-end against the real app as of this writing.
- USB MSC can be held mounted on demand via new debug console commands, `"MSC MOUNT"`/`"MSC UNMOUNT"` — useful for pulling a real file off the SD card for inspection, since this port's normal transient mount-then-unmount convention means the card otherwise only reports valid capacity to a USB host during the instant of an actual SD operation.

Segment sync, navigation/route upload, and notification passthrough are still **not implemented**. Introspect via the `"LEZ STATUS"` RTT console command.

**Maps** — feasibility study and implementation, see `docs/maps_feasibility.md` for the design and `docs/PORT_HISTORY.md` for the build-out log. Real OSM data (fetched via Overpass API, converted with `tools/osm_to_tiles.py`) renders correctly on real glass — confirmed by the user ("I could recognize streets"). SD-card-backed tile loading (not just flash-embedded) validated end-to-end with heavy concurrent I/O. Zoom is a fixed constant (`MAP_RENDER_ZOOM_LEVEL`); no pan/zoom input exists yet.

**RISC-V sandbox spike** — a from-scratch RV32IM interpreter for sandboxing future guest/plugin code (`stravaV11_app/lib/source/riscv/rv32_emu.{h,c}`), validated on `native_sim` only, not wired into `stravaV11_fw`/real hardware. Independent research track, not part of the phased port plan.

### Command channels on the real board

The real PCB's one UART is dedicated to the GPS module and RTT is used for console/logging, so on-device interactive commands (`"SIM START"`, `"DM SEARCH HRM"`, `"BATT"`, `"LEZ STATUS"`, etc., see `cmd_console.c`) go over the **RTT down channel** (`SEGGER_RTT_Read(0,...)`), driven from the host via pyocd's RTT classes — **not** USB CDC-ACM. CDC-ACM was tried first but the host's ModemManager service treats the board's generic CDC-ACM interface as a modem-probe candidate and stalls every write ~35s; a udev-rule fix is recorded in `todo.md` but not applied.
