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

**Business logic** — done. Ported and validated on both `native_sim` and real hardware: routes, PowerZone/RRZone/SufferScore/UserSettings, filters/kalman/VParser/komoot, `Locator`/TinyGPS++ (real NMEA parsing wired through on the real board, including a GPS simulation feature for indoor testing — `"SIM START"`/`"SIM STOP"` console commands). **Real GPS UART reading is now continuous** (`stravaV11_fw/src/gps_uart_demo.{h,c}`, added 2026-09-12 — until then, real GPS was only ever read for a fixed 2-second window at boot, `main.c`'s old `uart_demo()`; `"SIM START"` was the only continuous source, on real hardware or not). Real and simulated GPS both feed the same shared `Locator` object via `locator_encode_char()`, so `"SIM START"`/`"SIM STOP"` now pause/resume the real UART reader (mutual exclusion, not a race — two sources writing into one NMEA character parser at once would corrupt both). The real UART side uses Zephyr's interrupt-driven UART API (ISR drains the hardware FIFO into a ring buffer; a periodic work item drains that into the parser) — a plain periodic `uart_poll_in()` poll was tried first and found, on real hardware, to corrupt nearly every sentence at 9600 baud (confirmed via constant TinyGPS++ "Wrong checksum!" that a tight busy-loop never showed). Not ported: `Model.cpp` (orchestrator), `Boucle*.cpp` (ride-mode controllers), UserSettings' real FRAM persistence (adapter stub returns `false`, always falls back to in-memory defaults).

**Drivers/sensors** — `bme280`, `fxos8700`, FRAM (`mb85rcxx`), QSPI NOR flash (incl. XIP/memory-mapped mode), SD card (raw + FAT/LFN), and `STC3100` (custom driver, real battery data) are all done and validated on real hardware. `ms5637` and `VEML6075` have no custom driver (low priority — `ms5637` isn't the active barometer choice; `VEML6075` may not even be populated on the real board). **The STC3100 also plays a second, boot-critical role**: the board's regulator only stays on while the chip's IO0 pin is actively driven, so `stc3100_power_latch_hold()` runs as the literal first line of `main()` in `stravaV11_fw`, with a periodic 5s re-assertion — do not remove or delay this call.

**Display** — Zephyr's upstream `sharp,ls0xx` driver is reused directly (no custom driver needed). `ZephyrGFX` bridges `Adafruit_GFX` (ported unmodified) onto it. Confirmed correct on real glass by the user ("Looks perfect"). The real board's `cs-gpios` polarity must be `GPIO_ACTIVE_HIGH` (the driver hardcodes `SPI_CS_ACTIVE_HIGH`) — this was a real wiring bug once, fixed. VCOM is hardware-toggled on this board; the `serial-vcom-*` devicetree properties are deliberately absent (not needed, and not present in the pinned v3.2.4 binding anyway).

**Connectivity** — ANT+ (wildcard RX, HRM/BSC/FEC profiles, device-manager search/pairing) and BLE central (`bt_cp_client` for Cycling Power, `bt_hrs_client` for Heart Rate) are both done and validated concurrently on real hardware with real sensors (a Favero Assioma PRO power meter, a real HRM strap on both ANT+ and BLE simultaneously). BSC still uses a hardcoded, non-wildcard device number (deprioritized by user decision, not chased further). `glasses.c` (a custom ANT+ display-glasses protocol) is the one unported ANT+ profile. **ANT+ is now `ANT_ENABLED`-gated at build time, default OFF** (see the Bootloader/DFU flash-budget section below) — pass `-DANT_ENABLED=ON` to get it back for real-hardware ANT+/HRM/BSC/FEC testing. **Power and heart-rate consumers no longer pick a radio themselves** (added 2026-09-12): `stravaV11_fw/src/power_provider.h`/`hrm_provider.h` each expose one call (`power_provider_get_watts()`/`hrm_provider_get_bpm()`) that transparently picks ANT+ (`fec_demo.h`/`hrm_demo.h`) or BLE (new getters on `ble_demo.h`) — BLE preferred whenever fresh, but *sticky*: each provider only re-evaluates which source to use once its currently-selected source's own reading goes stale (5s), so a sensor visible on both radios at once can't cause tick-to-tick flip-flopping. `ride_recorder.c` and `model_glue.c` both go through these now instead of calling `fec_demo.h`/`hrm_demo.h` directly. **Cadence is unified too** (`cadence_provider.h`, same day, corrected scope after initially skipping it): a power meter reports its own cadence on *both* transports, not just the dedicated ANT+ speed/cadence sensor (`bsc_demo.c`) — ANT+ FE-C's page 25 carries it directly (`fec_demo_get_cadence_rpm()`, previously decoded and discarded), BLE's Cycling Power Measurement only carries cumulative crank revolutions + a last-event timestamp, so cadence is derived via the same rollover-aware delta technique `bsc_demo.c`'s own `calculate_cadence()` already uses (duplicated in `ble_demo.c`, not shared — the two use different underlying types). Three-way precedence, same sticky-until-stale rule: BLE, then ANT+ BSC (dedicated sensor over a power meter's incidental field), then ANT+ FE-C. Heart-rate's R-R interval still isn't unified (no real consumer). Real dual/triple-source precedence (a sensor actually reachable on more than one path at once) hasn't been verified against real hardware yet for any of the three providers — plumbing only, via `SIM START`.

**Scheduling/power** — `task_manager`/`power_scheduler`/`i2c_scheduler` all replaced with native Zephyr threads/`k_event`/`k_work_delayable`, validated running concurrently on real hardware.

**USB** — CDC-ACM done, validated with real host-side enumeration. Per the `project_hardware_and_status_corrections` memory, **USB MSC is also actually done now** — treat any older note claiming otherwise as stale.

**Bootloader/DFU** — MCUboot boots the app; a full BLE/MCUmgr SMP OTA update cycle (upload → swap → boot) validated end-to-end on real hardware. **Flash budget is tight and real**: the real board uses a static, asymmetric partition layout (`stravaV11_fw/pm_static_stravav11_nrf52840.yml`, primary 124 / secondary 123 sectors as of the 2026-09-11 ride-recording-feature rebalance — check that file's own comments for the current split and the true documented per-slot budget) computed by hand against MCUboot's true `app_max_size()` (which reserves more than a flat linker FLASH-region check shows). **Always check `zephyr.signed.bin`'s exact byte size against that documented budget before trusting a build** — `west build`'s flat "NN% used" summary is a different, more forgiving number and will not catch an overflow; a build that links and signs fine can still fail MCUboot's real swap-move validation *at boot* (spins forever in MCUboot's own fault-hardened halt loop — looks exactly like a hung/crashed board, not an oversized image; confirmed for real 2026-09-11, see `project_stravav11_fw_gotchas` memory). Do not trim existing bring-up/demo code in `main.c` to recover budget (explicit user direction) — use `STRAVA_SEGMENTS_ENABLED=OFF`/`ANT_ENABLED=OFF` (both default OFF) or WRN-level logging instead.

**Flash-saving build options** (both CMake `option()`s, both default OFF, both in `stravaV11_fw/CMakeLists.txt`): passing either flag after `--` on the *initial* `west build` doesn't reach the sysbuild sub-image's CMake cache — re-run `cmake -DFOO=ON <build-dir>/stravaV11_fw` directly against the sub-image dir, then `ninja`, if you need to toggle one on an existing build dir.
- `STRAVA_SEGMENTS_ENABLED` — excludes `Segment.{h,cpp}`/`SegmentManager.{h,cpp}` and segment-drawing code in `VueCRS`/`VuePRC`. Saves ~5.6KB.
- `ANT_ENABLED` (added 2026-09-11) — excludes `ant_demo.c`/`hrm_demo.c`/`bsc_demo.c`/`fec_demo.c`/`ant_dm_demo.c`, `lib/ant_fec.c`, and sdk-ant's own precompiled `libant.a`. Saves **~41KB** (measured: 458KB signed vs ~499KB with ANT+ in). `model_glue.c`/`ride_recorder.c`/`menu_content.cpp` still call into the HRM/BSC/FEC/device-manager API unconditionally as the live-sensor-data bridge — `src/ant_stubs.c` (compiled only when `!ANT_ENABLED`) provides always-"never paired" stub implementations of exactly those functions so the link still succeeds. `lib/ant_device_manager.c` stays unconditionally compiled either way (no dependency on the real ANT+ stack). With ANT+ off, `bt_enable()` alone brings up MPSL, same as any BT-only build.

**Ride recording** (`stravaV11_fw/src/ride_recorder.{h,c}`) — FIT export via a crash-safe QSPI append log (`ride_storage_partition`, one 2MB slot per ride, FRAM-tracked for resume across a reboot mid-ride) finalized to a real `.FIT` file on the SD card at `RIDE STOP`. **Multi-lap support added 2026-09-12**: a `"LAP"` console command closes the current lap and starts a new one, computing real average power and Normalized Power (30s rolling average^4, standard Coggan/TrainingPeaks definition) plus altitude stats per lap. Scales to 80+ laps with negligible FRAM cost — each closed lap is written immediately into the same QSPI stream as GPS records (its own FIT local mesg id, 1, interleaved with records' id 0), so only the *currently open* lap's live accumulators plus the last 5 *closed* laps (for display + reboot survival) need FRAM space; the full lap history is already durable in the QSPI stream itself, same guarantee the GPS records have. A new `VueLap` screen (`stravaV11_fw`-only — ride recording has no `stravaV11_app`/`native_sim` equivalent) shows the current lap and recent ones, reachable via `"VUE LAP"`/`"VUE DEBUG"` console commands (no buttons wired to screen switching yet). Power and heart-rate bpm go through `power_provider.h`/`hrm_provider.h` (added 2026-09-12, see the **Connectivity** bullet below) rather than the ANT+-only modules directly — real numbers still need a real power meter/HRM strap paired (ANT+ or BLE) to verify against; `SIM START` alone reads 0 for both.

**Lezyne feature** ("LE GPS 12" BLE identity, talks to Lezyne's real "GPS Ally" app): BLE plumbing + FIT file list/download/delete (`stravaV11_fw/src/lezyne_ble.{h,c}`, `lezyne_handler.{h,c}`) **fully validated end-to-end against the real GPS Ally app, crash-free** — connect, list, download, and delete all confirmed working on real hardware with a real phone, verified via a concurrent `adb logcat` capture showing zero crashes for the whole session. Getting there took several real fixes found only by testing against the real app (decompiled `LezyneCycleComputerDevice.txt`/`BluetoothLeService.txt`/`Lezyne_app.md` at the repo root, plus a full `jadx` decompile of the installed APK for exact crash stack traces, are the reverse-engineering notes):
- The device must advertise a hand-rolled GATT service using the *real* Lezyne UUIDs (`904d0001-2ce9-078d-944d-263fd93d95b2` service, `904d0002`/`904d0003` RX/TX) plus the standard Location and Navigation service (`0x1819`/`0x2A67`) — GPS Ally's own `onServicesDiscovered()` disconnects immediately if either is missing. The original NUS-based transport (this port's *and* legacy stravaV10's own guess) was simply wrong.
- The device's BLE identity address must end in `37:B4` (`isLezyneDevice()`'s own hardcoded address-suffix check) — forced via `bt_id_create()` in `ble_demo.c` before `bt_enable()`.
- FatFS/SD work triggered by a real phone session (file list, and especially the periodic download-chunk read) must run on a dedicated thread with its own generously-sized, isolated stack — doing it on the shared Bluetooth RX thread or the system workqueue caused two real stack-overflow crashes during testing. All of it (RX command processing + the download tick) now runs on one dedicated thread in `lezyne_handler.c`.
- FIT `time_created`/`timestamp`/`start_time` fields need the FIT epoch (seconds since 1989-12-31 UTC per `fit_example.h`'s own `FIT_DATE_TIME` comment), not Unix epoch (1970) — fixed via a `fit_timestamp_from_unix()` helper in `ride_recorder.c`.
- A real GPS Ally NPE crash on every download completion (`Ride.onMesg(LapMesg)`/`onMesg(SessionMesg)` call `.intValue()` on several FIT accessors — LAP's `avg_altitude`/`min_altitude`, SESSION's `total_ascent`/`total_descent`/`total_calories`/`enhanced_min_altitude`/`enhanced_max_altitude` — with no null check) since this port's exported FIT file never set any of them. Fixed by uncommenting those fields in `lib/libraries/fitLib/fit_example.{h,c}` (Garmin's stock FIT SDK template files ship with most optional fields commented out — see that fix's commit for the positional-serialization gotcha: struct member order and the field-def byte-layout table order must match exactly, and `FIT_xxx_USER_MSG_FIELDS_NB`/`FIT_xxx_MESG_SIZE` must be bumped in lockstep) and computing real running min/max/avg altitude and total ascent/descent per ride in `ride_recorder.c`.
- A second, independent epoch bug: the exported FIT *filename* (echoed back as the Lezyne wire protocol's `file_id`) was named with raw Unix time, but the real app's own `BleFitFile.parseFileName()` treats that value as FIT-epoch seconds when computing the file-list display date — so the list title showed ~20 years in the future even though downloaded file content (fixed above) decoded correctly. Fixed by applying the same `fit_timestamp_from_unix()` conversion to the filename in `ride_export_slot_to_sd()`.
- USB MSC can be held mounted on demand via debug console commands, `"MSC MOUNT"`/`"MSC UNMOUNT"` — useful for pulling a real file off the SD card for inspection, since this port's normal transient mount-then-unmount convention means the card otherwise only reports valid capacity to a USB host during the instant of an actual SD operation.
- `adb` is not installed on this dev machine by default and needs `sudo` to add; a phone must be in File Transfer/MTP (not "charging only" or MIDI) USB mode before `adb devices` will see it at all.

Segment sync, navigation/route upload, and notification passthrough are still **not implemented** — segments were descoped 2026-09-07 purely for flash budget (see `ANT_ENABLED`/`STRAVA_SEGMENTS_ENABLED` above) and **will be re-scoped later** (explicit user direction, 2026-09-12). That descope was audited 2026-09-12 against a real `jadx` decompile: `lezyne_handler.c`'s ignore-list covers every segment/route opcode the real app actually sends (confirmed by grepping the whole decompiled app for each opcode's real call sites — several opcodes in `l_protocol.h`, `NavigationNewRoute`/`RouteData`/`Step` and `RouteFileUpload*`, turned out to be dead/legacy and unused by the real app), and ignoring them produces the app's own graceful ~60s-timeout failure, not a hang or crash. The real segment/polyline/CRC16 wire formats (reverse-engineered from the decompile, not yet implemented) are recorded in the `project_lezyne_segments_protocol` memory for whenever the re-scope happens. Introspect via the `"LEZ STATUS"` RTT console command.

**Maps** — feasibility study and implementation, see `docs/maps_feasibility.md` for the design and `docs/PORT_HISTORY.md` for the build-out log. Real OSM data (fetched via Overpass API, converted with `tools/osm_to_tiles.py`) renders correctly on real glass — confirmed by the user ("I could recognize streets"). SD-card-backed tile loading (not just flash-embedded) validated end-to-end with heavy concurrent I/O. Zoom is a fixed constant (`MAP_RENDER_ZOOM_LEVEL`); no pan/zoom input exists yet.

**RISC-V sandbox spike** — a from-scratch RV32IM interpreter for sandboxing future guest/plugin code (`stravaV11_app/lib/source/riscv/rv32_emu.{h,c}`), validated on `native_sim` only, not wired into `stravaV11_fw`/real hardware. Independent research track, not part of the phased port plan.

### Command channels on the real board

The real PCB's one UART is dedicated to the GPS module and RTT is used for console/logging, so on-device interactive commands (`"SIM START"`, `"DM SEARCH HRM"`, `"BATT"`, `"LEZ STATUS"`, etc., see `cmd_console.c`) go over the **RTT down channel** (`SEGGER_RTT_Read(0,...)`), driven from the host via pyocd's RTT classes — **not** USB CDC-ACM. CDC-ACM was tried first but the host's ModemManager service treats the board's generic CDC-ACM interface as a modem-probe candidate and stalls every write ~35s; a udev-rule fix is recorded in `todo.md` but not applied.
