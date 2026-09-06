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
