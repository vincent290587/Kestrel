# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository purpose

This repo is porting `stravaV10` — a bicycle GPS computer firmware for a custom nRF52840 PCB, currently on Nordic's legacy nRF5 SDK v16 + s340 (BLE+ANT) softdevice — to nRF Connect SDK (sdk-nrf) + Zephyr. `stravaV10/` is the original firmware (its own git repo/remote, unrelated history); `ncs/` is the new west workspace for the port. There is no ported application yet — only the toolchain/workspace has been stood up so far.

## Commands

### Zephyr/NCS workspace (`ncs/`) — the active port target

Activate the environment first, in every new shell:
```
source /home/vincent/Github/StravaV11/.venv/bin/activate
cd /home/vincent/Github/StravaV11/ncs
```

Build (from inside `ncs/`):
```
west build -p always -b native_sim <app-dir> -d <build-dir>       # host simulation, no hardware
west build -p always -b nrf52840dk/nrf52840 <app-dir> -d <build-dir>  # target dev kit
```
Flash and read console (dev kit only, requires SEGGER J-Link tools + `nrfutil` — both already installed on this machine):
```
west flash --build-dir <build-dir>
# console is /dev/ttyACM0, 115200 8N1 (stty -F /dev/ttyACM0 115200 raw -echo)
nrfutil device list                          # confirm the DK is detected
nrfutil device reset --serial-number <sn>    # reset without reflashing
```
Toolchain facts: NCS v3.3.4, Zephyr SDK 0.17.4 with only the `arm-zephyr-eabi` toolchain installed (target is Cortex-M4 only — do not install other archs). `west sdk list` shows what's registered.

### Legacy firmware (`stravaV10/`) — reference only, not actively built here

Real hardware build requires the nRF5 SDK v16 tree + GCC 6 2017-q2-update ARM toolchain (neither is installed in this environment — only the new Zephyr `arm-zephyr-eabi` toolchain is):
```
cd stravaV10/pca10056/s340/armgcc
make                    # needs Makefile.local (gitignored, machine-specific) defining SDK_ROOT
make flash_softdevice   # or: make dfu_softdevice
make flash              # or: make dfu
```

Host-side test/simulation build (business logic + a mocked LCD/GPS/Nordic-API layer, runs on Linux with no target hardware — this is the thing to reuse when porting logic, since it already proves the code is hardware-decoupled):
```
cd stravaV10
mkdir build && cd build
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug ..
make
./StravaV10
```
`TDD/LS027simulator.jar` renders what the real Sharp LS027 display would show, when built with `-DLS027_GUI` enabled. `TDD/GPX_simu*.csv` are canned GPX replay inputs used by the simulator instead of a live GPS fix.

## Architecture

### Port plan and decisions already made

The full component-by-component inventory (what's hardware-agnostic and portable as-is, what Zephyr replaces outright, what needs a real driver port, and the two biggest feasibility risks — ANT+ support on NCS/Zephyr, and rewriting the BLE central profiles against Zephyr's Bluetooth host API) lives in this session's memory, not duplicated here since it will get stale as the port proceeds; ask about it if picking this project back up mid-port. Decisions locked in so far: ANT+ is being kept (not dropped for BLE-only), the first porting milestone is the hardware-agnostic business-logic layer (`Model`, `routes`, `Boucle*`, `filters`, `kalman`, `VParser`, `komoot_nav.c`) validated on `native_sim` before any driver/board work, hardware bring-up targets the nRF52840-DK before the custom PCB, and the separate `AP/` companion-dongle firmware is out of scope for this effort.

### `stravaV10` structure (legacy firmware being ported)

- **Business logic (hardware-agnostic C/C++, proven portable by the TDD host harness)**: `source/Model.*`, `source/model/*` (Attitude, Locator, PowerZone, RRZone, SufferScore, UserSettings), `source/routes/*` (GPX/segment geometry), `source/model/Boucle*` (CRS/FEC/Zwift ride-loop state machines), `libraries/filters`, `libraries/kalman`, `libraries/VParser`, `libraries/komoot/komoot_nav.c`.
- **UI**: `source/vue/*` (menu system, screens per mode) drawn via `libraries/AdafruitGFX` onto the custom `drivers/lcd/ls027.c` Sharp memory LCD driver.
- **Connectivity**: `libraries/ant_profiles/*` (ANT+ FE-C/HRM/cadence-speed/glasses) and `libraries/ble_services/*` (`ble_cp_c`, `ble_lns_c`, `ble_komoot_c` — BLE GATT central profiles), both written against SDK16 APIs (`nrf_ble_*`, Peer Manager, ANT stack) that don't exist on Zephyr.
- **Sensors**: `source/sensors/*` — `bme280` (baro) and `fxos` (FXOS8700 accel/mag) have upstream Zephyr drivers; `ms5637`, `STC3100` (fuel gauge), `VEML6075` (UV), `fram` do not and need custom Zephyr drivers.
- **Storage**: `source/sd/*` (SD/FatFS) and `libraries/SST` (external NOR flash chips) map onto Zephyr's `disk_access`/`flash`/`fs` subsystems.
- **SDK16 glue that gets trashed, not ported**: `libraries/task_manager`+`task_manager_wrapper*` (Zephyr threads/work queues/timers instead), `drivers/gpio.c`/`i2c.c`/`spi.c`/`uart.c` (devicetree + Zephyr drivers instead), `libraries/hardfault`, `helper.*`/`math_wrapper*`/`assert_wrapper.h`, `dfu/` + the old bootloader (MCUboot instead), `pca10056/`/`hw_test/`/`custom_board_v*.h` (new devicetree board def instead), `libraries/rtt`/`sysview`/`jscope` (Zephyr's native RTT/SystemView backends instead).
- **Out of scope**: `AP/` (separate BLE-UART bridge dongle firmware), `zpm/` (Node.js PC-side GPX/segment tooling, not firmware).

### `ncs/` structure (new west workspace)

Standard NCS layout: `nrf` (the manifest repo, sdk-nrf), `zephyr` (pinned via NCS manifest, currently `ncs-v3.3.4`), `nrfxlib`, `bootloader` (mcuboot), `modules`, `tools`.

`stravaV11_app/` is the freestanding application holding the Phase 1 port (see below) — the only application under this workspace so far. Build it with:
```
west build -p always -b native_sim/native/64 stravaV11_app -d <build-dir>       # host, run zephyr.exe directly
west build -p always -b nrf52840dk/nrf52840 stravaV11_app -d <build-dir>        # DK, then west flash --build-dir <build-dir>
```
Use the `native_sim/native/64` qualifier, not plain `native_sim` — the latter defaults to a 32-bit build, and this machine's g++ 14 doesn't have the 32-bit multilib libstdc++ headers installed (only g++13's are present), so it fails with a `bits/c++config.h: No such file or directory` error.

#### Phase 1 status: business-logic port (done, validated on both native_sim and the DK)

`stravaV11_app/lib/` holds copies (not symlinks — `stravaV10` stays untouched as the reference) of the hardware-agnostic tier identified in the architecture survey: `source/routes/*`, `source/model/{PowerZone,RRZone,SufferScore,UserSettings}`, `source/display/{SegmentManager,Zoom}`, `libraries/{kalman,VParser,komoot,filters,utils}`, plus the small SDK16 wrapper headers (`segger_wrapper.h`, `math_wrapper.h`, `assert_wrapper.h`, `task_manager_wrapper.h`) — these are kept building under `-DTDD`, reusing stravaV10's existing host-build abstraction (they already compile to hardware-free stubs that way) rather than writing new Zephyr-native shims prematurely; that substitution is follow-up work for when the driver phases land. `src/main.cpp` is a smoke test (not a ztest suite yet) exercising each ported piece; output is bit-identical between native_sim and the flashed DK.

Three things worth knowing if you touch this again:
- **`libraries/filters/order1_filter.c` was swapped for `TDD/order1_filter_tdd.c`.** The former is the real ARM CMSIS-DSP implementation (references a `.instance` struct member that only exists in the non-`TDD` branch of `order1_filter.h`) — stravaV10's own CMakeLists.txt already substitutes the TDD variant for host builds; we do the same.
- **`Point::objectCount` / `Point2D::objectCount2D`** (static members declared in `Points.h`) are defined in stravaV10's `Model.cpp`, not `Points.cpp` — an artifact of Model.cpp being the catch-all file, not a bug. Since Model.cpp isn't ported, these two one-line definitions live in `src/globals.cpp` instead, along with the `UserSettings u_settings` global instance that `PowerZone::addPowerData()` reads FTP from.
- **Adapters for the two real hardware calls this tier still makes**, in `stravaV11_app/adapters/`: `notifications_segNotify()` (drives the WS2812 status LED from `SegmentManager`) no-ops; `fram_read_block()`/`fram_write_block()` (from `UserSettings::sync()`/`writeConfig()`) return `false`, so settings never persist and always fall back to `resetConfig()`'s in-memory defaults. Both get replaced for real once the LED and FRAM driver phases happen. `ant_device_manager.h` here is also a placeholder — just the four default ANT device-number constants `UserSettings::resetConfig()` needs, not the real header (which pulls in the full ANT stack via `ant.h`).

Not part of this slice, and why: `Model.cpp` (the hardware/connectivity orchestrator), `Boucle*.cpp` (ride-mode controllers — `Boucle.cpp` itself pulls in the task manager, global `vue`/`stc` objects, and `g_structs`-based error state; `BoucleFEC.cpp` pulls in ANT+ FE-C and BLE), `Attitude.cpp`/`Locator.cpp` (need the FXOS sensor driver and TinyGPS++ integration respectively), `UserSettings`'s FRAM persistence, and all connectivity (`ble_services`, `ant_profiles`) — these need actual driver/connectivity phases first, per the phased plan.

#### Phase 2 status: DK driver bring-up (in progress)

`stravaV11_fw/` is a second, separate application (not merged into `stravaV11_app`) for hardware bring-up on the nRF52840-DK: GPIO/LED/button and the LS027 display. It's kept separate because it depends on DK-specific devicetree (an `&arduino_spi` node, a `zephyr,display` chosen node) that doesn't exist on `native_sim` — mixing it into the native_sim-validated logic app would break that app's portability. The two apps get merged once both are mature.
```
west build -p always -b nrf52840dk/nrf52840 stravaV11_fw -d <build-dir>
west flash --build-dir <build-dir>
```

Key finding: **Zephyr already has an upstream driver for this exact display.** `zephyr/drivers/display/ls0xx.c` (devicetree compatible `sharp,ls0xx`) explicitly lists `LS027B7DH01A` as supported hardware, and its binding's `serial-vcom-inversion` property matches stravaV10's own `drivers/lcd/ls027.c` approach (VCOM toggled in-band over SPI, no EXTCOMIN pin) — so no custom driver was needed, just a devicetree overlay (`stravaV11_fw/boards/nrf52840dk_nrf52840.overlay`) wiring a `sharp,ls0xx` child node (400x240, `serial-vcom-inversion`) onto the DK's Arduino-header SPI bus (`arduino_spi`/spi3). There's also a matching Zephyr shield (`boards/shields/ls0xx_generic`) but it defaults to a different variant (128x128, hardware EXTCOMIN) — not a match for our wiring, hence the hand-written overlay instead of `-DSHIELD=`.

**Validated on real hardware** (bare DK, nothing external attached — see [[project_port_scope]]/CLAUDE.md decisions):
- GPIO: on-board LED toggling and button read.
- UART: implicitly proven the whole time — the console itself is over UART (J-Link's CDC-ACM bridge).
- I2C: a full 0x08-0x77 address scan on `arduino_i2c` (i2c0) comes back as 112 clean NACKs, 0 ACKs — expected with no sensor attached, and importantly the bus completes every transaction rather than hanging.
- SPI + display: the display subsystem initializes (`spi_is_ready_dt()` passes — it only checks the SPI *controller*, not a peripheral handshake, so this succeeds even with nothing wired to the bus) and `display_write()` returns 0 for a full-frame test pattern.

**Not validated, and can't be until the display is physically wired up:** that the SPI protocol framing actually produces correct pixels on real glass, or the exact pin assignment (the overlay's pins are arbitrary DK Arduino-header pins, explicitly not the final custom-PCB mapping — that comes in the later custom-PCB bring-up phase). Phase 2 is otherwise done.

#### Phase 3 status: sensors (partly done — two free wins, three need real custom driver work)

Added to `stravaV11_fw`'s overlay/prj.conf/main.c, same bare-DK caveat as the display (nothing physically attached, so these validate clean init/error paths, not real readings):

- **`bme280`** (0x76) — Zephyr's upstream `bosch,bme280` driver, direct reuse. This is stravaV10's *active* barometer (`BARO_TYPE bme280` in `source/parameters.h`); `ms5637.c` in stravaV10 is an alternate-BOM option for different hardware revisions and shares the same 0x76 address (the two are never populated on the same board).
- **`fxos8700`** (0x1e) — Zephyr's upstream `nxp,fxos8700` driver, direct reuse. On the DK it correctly logs `Could not get WHOAMI value` and reports not-ready — proves the presence-check path works, not that it works with a real chip yet.
- **FRAM → `fujitsu,mb85rcxx`** (0x50, `address-width = 8`, `size = 2048`) — reused, not custom-built. Traced stravaV10's `source/sensors/fram.c` addressing scheme (`twi_address |= (block_addr & 0x700) >> 8`, i.e. high address bits folded into the low bits of the 7-bit I2C address) against the Zephyr driver's `mb85rcxx_translate_address()` (`cfg->i2c.addr + (offset >> cfg->addr_width)`) and confirmed they're the same scheme — not a guess. `eeprom_write()` correctly returns `-5` (I/O error) with nothing attached.

**Still need real custom Zephyr drivers** (not written yet — this is genuine remaining work, not just unvalidated):
- **`ms5637`** — Zephyr has `drivers/sensor/meas/ms5607` (a sibling chip) but it is **not** a safe alias: compared stravaV10's own `ms5637_compute_t_p()` against the ms5607 driver's `ms5607_compensate()` line by line. The first-order compensation and coefficient layout match, but the second-order low-temperature term differs (`sens2 = 29 * tx / (1<<4)` in stravaV10's MS5637 code vs `SENSi = 2 * temp_sq` in the ms5607 driver) — aliasing the two would build and run fine but silently give a slightly wrong reading below 20°C. Since `ms5637` isn't even the active barometer choice, this is low priority.
- **`STC3100`** (fuel gauge/coulomb counter, address 0x70) — no upstream analog; Zephyr's `sbs_gauge` driver targets SBS-compliant gauges, a different protocol family entirely.
- **`VEML6075`** (UV sensor) — no upstream analog; Zephyr has `veml7700`/`veml6031`/`veml6046` (different Vishay parts, different register maps), none matching VEML6075. Gated behind `PROTO_V11` in stravaV10, which — despite the name — is the flag for the *current* (v3) board revision, not a deprecated prototype, so this sensor is genuinely part of the active hardware, just not yet portable for free.

#### Phase 4 status: storage (done — real hardware validated for flash, SD stays a clean-failure placeholder)

- **QSPI NOR flash — fully validated on real hardware, not just a clean-init check.** The nRF52840-DK has a genuinely populated on-board QSPI NOR chip (MX25R6435F, 64Mbit, `&qspi`/`mx25r64` in the DK's own default devicetree — no overlay needed to reach it). `qspi_flash_demo()` in `stravaV11_fw/src/main.c` does a real `flash_erase()` → `flash_write()` → `flash_read()` round trip against it and the pattern comes back byte-for-byte correct. stravaV10's own external flash is a different chip (MT25QL128, `libraries/SST/mt25.c`) but the same JEDEC SPI-NOR-over-QSPI family, so this is a legitimate stand-in for that storage path — the first piece of Phase 2-4 that's actually hardware-proven end-to-end, not just "doesn't hang."
- **Original plan was FAT-over-QSPI-flash** (`zephyr,flash-disk` + `disk_access` + `CONFIG_FAT_FILESYSTEM_ELM`, mirroring stravaV10's actual FatFs usage) but hit a real NCS-specific wall: this workspace's Partition Manager is unconditionally active for any app built here (its Kconfig literally says "read-only, do not change" — it isn't something `prj.conf` can turn off), and `flashdisk.c`'s build only works for partitions PM itself allocated and tagged with "disk" affiliation metadata. A hand-written devicetree `fixed-partitions` node isn't visible to PM's scan, which broke the build (`PM_FOREACH_AFFILIATED_TO_disk` expanding to nothing). The real fix is a `pm_static.yml` statically registering the partition with PM — a genuine follow-up item, not done here; descoped to the direct flash API instead, which still proves the chip works, just not the filesystem layer on top of it.
- **SD card over SPI** (`zephyr,sdhc-spi-slot` + `zephyr,sdmmc-disk`, placeholder pins on `arduino_spi`, same caveat as the display/sensors) — cleanly reports "no card" (`Card error on CMD0`, `disk_access_ioctl` returns `-116`) rather than hanging. stravaV10's SD path (`source/sd/diskio_sdc.c`) is the old nRF5 SDK's `nrf_block_dev_sdc` component doing the same SD-over-SPI protocol.

### Phase 5 status: connectivity — ANT+ feasibility spike done (research only, no code yet)

Nordic's own sdk-nrf ships **zero** ANT+ code (confirmed by grepping the actual `ncs/` checkout — nothing beyond an unrelated `variant` substring match). ANT+ is technically feasible, but only via a separate, access-gated add-on:

- **"ANT for nRF Connect SDK"** (https://ant-nrfconnect.github.io/), published by ANT Wireless (a Garmin division), currently v2.1.1. Explicitly supports nRF52840. Confirmed compatibility matrix pairs sdk-nrf v3.2.4 with sdk-ant v2.1.0/2.1.1 — our pinned v3.3.4 isn't explicitly listed, so **verify/re-check version compatibility before starting real integration work**, don't assume the patch gap is fine.
- **Concurrent operation with Bluetooth is a documented, real use case**, not experimental: `CONFIG_BT` + `CONFIG_ANT` together, described as "Multiprotocol: ANT and Bluetooth® LE" — this is exactly what stravaV10 needs (ANT+ sensors + BLE central profiles simultaneously). The underlying coexistence mechanism (MPSL timeslots, presumably, since `nrfxlib/mpsl/include/mpsl_timeslot.h` is present and this is the standard Nordic mechanism for this kind of sharing) isn't detailed in the public docs.
- **Access**: free "ANT+ Adopter" account (accept a license agreement, authenticate via GitHub) — no cost for a hobby/non-commercial project. Integration is a west manifest add-on step (not yet done — no `ant` module in this workspace).
- **Hardware**: requires the 32.768kHz LF crystal (X2) with ≤50ppm tolerance — both the nRF52840-DK and stravaV10's custom PCB have this already (standard for BLE timing regardless of ANT+).

**Ecosystem context surfaced during this research, reaffirmed with the user 2026-09-06**: Garmin ended the ANT+ certification and paid membership program on 2025-06-30 (EU Radio Equipment Directive now requires authenticated/encrypted wireless transmission of personal data like heart rate; ANT+'s optional encryption isn't enforced by the installed base, and Garmin chose to wind the program down rather than break compatibility). The free Adopter tier, device profiles, docs, and this SDK remain available — nothing here blocks this project — but industry commentary (DC Rainmaker, 5kRunner) describes the broader ecosystem as being in decline. User's call: proceed with ANT+ as originally planned regardless.

Not started yet: the actual `ant` west module integration, and `ble_lns_c`/`ble_komoot_c` (only `ble_cp_c` is ported so far — see below).

#### Phase 5 status: BLE central — Cycling Power Service client ported and validated on real hardware

`ble_cp_c` (Cycling Power Service, UUID 0x1818) is ported to `stravaV11_fw/lib/bt_cp_client.{h,c}`, following the structure of NCS's own `bt_hrs_client` (`nrf/subsys/bluetooth/services/hrs_client.c`) — the closest upstream analog, since neither Zephyr nor NCS ships a Cycling Power Service *client* (they have plenty of GATT *server* services like `hrs`/`bas`, but writing your own client against `bt_scan` + `bt_gatt_dm` is the normal NCS pattern, demonstrated by `nrf/samples/bluetooth/central_and_peripheral_hrs`, which `stravaV11_fw/src/ble_demo.c` mirrors). The wire-format parsing (`cp_measurement_parse()`/`cp_vector_parse()` in `bt_cp_client.c`) is a direct port of stravaV10's `power_measure_decode()`/`power_vector_decode()` — same Bluetooth SIG GATT Specification Supplement fields, just `sys_get_le16/32()` instead of Nordic's `uint16_decode`/`uint32_decode`. No pairing/bonding, matching stravaV10 (cycling power meters are normally open GATT servers).

**Validated on real hardware, not just build success**: flashed to the DK, the SoftDevice Controller and Bluetooth host fully initialize (`Identity: E6:14:C4:12:F1:E8 (random)`, HCI/LMP version negotiated), and scanning starts cleanly. Hit and fixed one real bug this way: `bt_scan_filter_add()` failed with `-ENOMEM` until `CONFIG_BT_SCAN_UUID_CNT=1` was set (its Kconfig default is 0 filter slots — not obvious from the API alone). No Cycling Power peripheral was nearby to complete the discovery/subscribe path, so — same tier as the sensors/display — this proves the BLE stack and scan/filter path work correctly, not that the notification parsing produces correct values against a real sensor yet.

**`ble_lns_c` (Location and Navigation Service) and `ble_komoot_c` (custom Komoot navigation service) are out of scope by user decision (2026-09-06) — not being ported.** `bt_cp_client` is the only BLE central profile from stravaV10 that's part of this port. Wiring `bt_cp_client` into the ported `Model`/`Boucle*` logic from Phase 1 is still a later step (same as the driver-to-logic wiring gap noted in earlier phases).

#### Phase 5 status: ANT+ integrated and validated on real hardware

The user obtained ANT+ Adopter access and downloaded `sdk-ant-2.1.1` (from `github.com/ant-nrfconnect/sdk-ant`) to `~/Downloads/sdk-ant-2.1.1/`, then copied into this workspace at `ncs/ant/` — **gitignored, never commit it** (proprietary, license-gated vendor source; see `.gitignore`'s comment). It's a proper Zephyr module (has `zephyr/module.yml`), so it's consumed via `ZEPHYR_EXTRA_MODULES` in `stravaV11_ant/CMakeLists.txt` rather than a west manifest import — this avoids the workspace-topology conflict raised during the feasibility spike (sdk-ant's own `west.yml`, used only when it's the manifest root, pins sdk-nrf v3.2.4; we stayed on our existing v3.3.4 per the user's explicit choice to "just try the current version" rather than creating a second workspace or downgrading).

**That version gap was real, not hypothetical, but narrow and fixed**: `ant/Kconfig`'s `ANT_LIB_DIR` (which selects the correct precompiled `libant.a` variant to link) computed empty because it only checked `SOC_SERIES_NRF52X`, a Kconfig symbol Zephyr has since deprecated and renamed to `SOC_SERIES_NRF52` (in our v3.3.4 / Zephyr 4.3.99 — it now only `select DEPRECATED`, selected by nothing for a real nRF52840 build). Fixed with a one-line local patch to `ant/Kconfig` (`default "nrf52" if SOC_SERIES_NRF52X || SOC_SERIES_NRF52`) — not upstreamed, just a local workaround since that file isn't tracked here anyway. `ANT_LIB_DIR` has no Kconfig prompt, so it can't be overridden from a `.conf` fragment (Zephyr's Kconfig rejects that outright) — the Kconfig source itself had to change.

`stravaV11_ant/` (new, separate from `stravaV11_fw` — kept isolated deliberately since this was the first-ever build of unfamiliar, version-mismatched vendor code) is a minimal test: `ant_init()` + one wildcard slave RX channel (device number/type/transmission type all 0, RF freq 66 / 2466MHz, period 8192 / 4Hz — same defaults as `ant/samples/ant_broadcast_rx`), evaluation license key (`CONFIG_ANT_EVALUATION_KEY=y` — appropriate since this is a non-commercial hobby project, not a product being sold). Uses `CONFIG_ANT` only, not yet combined with `CONFIG_BT` (the documented multiprotocol path) — that combination, and porting stravaV10's actual profiles (`rf/fec.c`/`hrm.c`/`bsc.c`/`glasses.c` against `ant/include/ant_profiles`), are still open.

**Validated on real hardware**: flashed to the DK, console shows `ANT stack initialized, version 2.01.01` and `ANT wildcard RX channel open, listening...` — the radio genuinely opened a channel, not just a clean error path. No ANT+ broadcaster was nearby to receive from, so (same tier as BLE/sensors) actual message reception is still unproven.

**Update: ANT+ and BLE now combined and validated together, in `stravaV11_fw` itself** (not just the isolated `stravaV11_ant` test). `stravaV11_fw` gained the same `ZEPHYR_EXTRA_MODULES` wiring for `ant/`, plus `CONFIG_ANT`/`CONFIG_ANT_CHANNEL_CONFIG`/`CONFIG_ANT_EVALUATION_KEY` alongside its existing BLE config, and a new `src/ant_demo.c` (the same wildcard-RX logic as `stravaV11_ant`). Call order matters and is deliberate: `main()` calls `ant_demo_start()` *before* `ble_demo_start()`, matching `ant/samples/ble_ant_app_hrm` (sdk-ant's own combined reference sample) — both ANT and the SoftDevice Controller sit on MPSL, and that sample brings ANT up first, with no `CONFIG_ANT_SDC_INIT` needed either way. Flashed to the DK: ANT initializes and opens its channel, then the SoftDevice Controller and Bluetooth host initialize immediately after with no errors, and both keep running concurrently (scanning + ANT RX) for 15+ seconds with no crash or conflict — the documented "Multiprotocol: ANT and Bluetooth LE" capability, genuinely confirmed on this hardware/SDK-version combination, not just asserted by the docs.

`stravaV11_ant` (the isolated single-protocol test) is left in place as a smaller reference/repro case; it isn't superseded, just no longer the only place this code runs.
