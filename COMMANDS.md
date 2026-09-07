# Command cheat sheet

Practical commands for working with this repo day to day. See `CLAUDE.md` for
architecture/history; this file is just "what do I actually type."

All commands below assume:

```bash
source /home/vincent/Github/StravaV11/.venv/bin/activate
cd /home/vincent/Github/StravaV11/ncs
```

(the venv provides `west`; commands must run from inside `ncs/`, the west
workspace root)

## Building

```bash
# stravaV11_app -- business logic, host simulation, no hardware
west build -p always -b native_sim/native/64 stravaV11_app -d <build-dir>

# stravaV11_fw -- hardware app, nRF52840-DK
west build -p always -b nrf52840dk/nrf52840 stravaV11_fw -d <build-dir>

# stravaV11_fw -- custom PCB (needs BOARD_ROOT, resolved before the app's
# own CMakeLists.txt runs, so it must be passed on the command line)
west build -p always -b stravav11/nrf52840 stravaV11_fw -d <build-dir> \
  -- -DBOARD_ROOT=$(pwd)/stravaV11_fw
```

Use `native_sim/native/64`, not plain `native_sim` (the latter defaults to a
32-bit build that fails to compile on this machine).

## Running the native_sim build

```bash
<build-dir>/stravaV11_app/zephyr/zephyr.exe
```

Runs forever (it's a live Zephyr image, not a batch job) -- background it or
pipe through `timeout`/`grep` and kill it once you've seen what you need.

## Flashing

```bash
# DK's own on-board J-Link (or any single-probe setup)
west flash --build-dir <build-dir>

# A specific probe, when more than one J-Link-family device is attached
west flash --build-dir <build-dir> -- --id <probe-serial-number>
```

Two probes have shown up in this project:
- `683282379` -- the nRF52840-DK's own on-board J-Link (used when the custom
  board is wired to the DK's SWD header instead of a standalone probe).
- `269302816` -- the standalone external J-Link probe, wired directly to the
  custom PCB's own SWD header. This is the normal/expected setup.

Check `nrfutil device list` to see which probe(s) are actually connected
right now before picking an `--id`.

## Listing / waiting on USB devices

```bash
# What's attached right now (J-Link probes + the board's own USB, if enumerated)
nrfutil device list

# Just the board's own USB (CDC-ACM + MSC), by VID
lsusb -d 2fe3:

# Block devices, including the SD/NOR MSC LUNs once the board enumerates
lsblk -o NAME,SIZE,MODEL,TRAN,MOUNTPOINT
```

Boot takes anywhere from ~5s to ~60s+ before `usb_demo_start()` runs and the
board's own USB shows up (varies with how far into `main()`'s subsystem
bring-up sequence it's gotten). Poll instead of guessing a fixed sleep:

```bash
timeout 60 bash -c 'until lsusb -d 2fe3: >/dev/null 2>&1; do sleep 3; done; echo up'
```

If a block device reads a wrong/zero size (`0B`) right after enumeration,
that's usually just Linux not having polled it yet -- retry `lsblk` after a
few seconds, or trigger the firmware's own disk init via the `DISK TEST`
command (see below) before assuming something's actually wrong.

## Sending commands to the firmware

Every long-running/destructive feature (GPS simulation, SD stress test, live
map screen, raw disk tests, SD format, status LED test colors) is gated
behind a text command, reachable over **either** transport -- same command
set, same effect, pick whichever is convenient:

```
SIM START / SIM STOP        -- replay the embedded GPX route into Locator (~1Hz)
STRESS START / STRESS STOP  -- heavy SD-card write/read cycle every 500ms
MAP START / MAP STOP        -- live GPS-driven map screen redraw loop
DISK TEST                   -- one-shot raw disk_access read/write round trip (SD + NOR)
FORMAT SD                   -- reformat the SD card's FAT filesystem (destroys all data on it)
LED RED / LED GREEN / LED BLUE -- fire a one-shot WS2812 status-LED pulse in that color
```

**Over USB CDC-ACM** (needs the board's own USB cable connected; use
`pyserial`, not a bash `exec N<>/dev/ttyACM0` redirect -- the latter has
proven unreliable, bytes sometimes silently never arrive):

```bash
python3 -c "
import serial
s = serial.Serial('/dev/ttyACM0', 115200, timeout=1)
s.write(b'MAP START\r\n')
s.flush()
s.close()
"
```

(port may enumerate as `/dev/ttyACM1` instead of `/dev/ttyACM0` if a DK's
own J-Link VCOM is also attached and claimed `ttyACM0` first -- check
`nrfutil device list`'s `Ports` field for the board's own entry, distinct
from the J-Link probe's entry)

**Over RTT** (needs the J-Link probe, not the USB cable):

```bash
cd stravaV11_fw
python3 tools/rtt_cmd.py <probe-serial> <capture-seconds> "SIM START"
```

Omit the trailing command argument to just capture output without sending
anything.

## Reading RTT output live

```bash
cd stravaV11_fw
OUT=/path/to/output.log
timeout 30 script -qec "pyocd rtt -u <probe-serial> -t nrf52840" "$OUT" >/dev/null 2>&1
grep -in "<something>" "$OUT"
```

RTT logging on the custom board is `CONFIG_SEGGER_RTT_MODE_NO_BLOCK_SKIP`
(non-blocking) -- messages produced while no reader is attached are dropped,
not queued, so a short capture that attaches mid-boot will miss early log
lines. For anything time-sensitive, flash and attach the RTT reader in the
same breath so nothing is missed:

```bash
west flash --build-dir <build-dir> -- --id <probe-serial>
timeout 30 script -qec "pyocd rtt -u <probe-serial> -t nrf52840" "$OUT" >/dev/null 2>&1
```

**Never** run repeated short RTT attach/detach cycles against older builds
that might still have `CONFIG_SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL` set --
that mode halts the CPU once the buffer fills with no reader attached, which
has caused a real board lockup + SD card corruption before. The custom
board's `stravav11_nrf52840_defconfig` has been reverted to the safe
non-blocking default; if you ever see `BLOCK_IF_FIFO_FULL` in a defconfig
again, that's a red flag, not routine tuning.

## Resetting / recovering the board

```bash
# Soft reset (re-runs the currently-flashed firmware, doesn't reflash)
nrfutil device reset --serial-number <probe-serial>

# Full chip erase + debug-port recovery -- use ONLY if SWD is unreachable
# ("Could not connect to the target device" from JLinkExe even at low speed)
# while the board's own firmware is otherwise clearly still running (e.g.
# USB CDC-ACM still enumerates). Destroys everything currently on the chip;
# reflash immediately after.
nrfutil device recover --serial-number <probe-serial>
```

Direct SWD connectivity check (bypasses `west`/`nrfutil`, useful when
diagnosing "is the debug port actually reachable" separately from "does
`west flash` work"):

```bash
cat > /tmp/probe.jlink << 'EOF'
r
q
EOF
/opt/SEGGER/JLink_V974/JLinkExe -USB <probe-serial> -nogui 1 -if swd -speed 4000 \
  -device nRF52840_xxAA -CommanderScript /tmp/probe.jlink
```

Look for `VTref=3.3XXV` (confirms power is present even if connect fails)
and either a clean `Cortex-M4 identified` or `Error occurred: Could not
connect to the target device`.

`west flash`/`JLinkExe` on this machine occasionally segfault or exit with a
nonspecific error (`status 1`, `status -11`) with no useful message --
that's usually just flaky tooling, not a real problem; retry once or twice
before concluding anything is actually wrong.

## SD card via USB MSC

The SD card mounts as a normal USB drive once the board enumerates (see
"Listing / waiting on USB devices" above) -- no special tooling needed to
read/write it, just a regular file manager or `cp`/`rsync` from a shell.

```bash
# If the host's mount looks stale (e.g. right after an on-device FORMAT SD),
# force a clean remount:
udisksctl unmount -b /dev/sdX
udisksctl mount -b /dev/sdX

# Bulk copy + integrity check pattern used for map tiles this project:
cp /path/to/tiles/*.bin /media/$USER/<volume-id>/
sync
# then verify byte-for-byte, e.g.:
for f in /path/to/tiles/*.bin; do
  b=$(basename "$f")
  cmp -s "$f" "/media/$USER/<volume-id>/$b" || echo "$b MISMATCH"
done
```

**Do not reset the board immediately after a large host-side write.**
`sync` only flushes the *host's* page cache -- it says nothing about
whether the SD card's own write-back has actually committed yet. A reset
shortly after a big copy has corrupted the FAT filesystem before (host
kernel: `fat_free_clusters: deleting FAT entry beyond EOF`, filesystem
forced read-only). Let the card sit idle for a bit, or cleanly
`udisksctl unmount` it, before triggering any board reset. If corruption
does happen, `FORMAT SD` (see "Sending commands" above) gives a clean,
deliberate way back to a known-good filesystem.

## Legacy stravaV10 firmware (reference only, not actively built here)

Real hardware build (needs the nRF5 SDK v16 tree + GCC 6 2017-q2-update ARM
toolchain, neither installed in this environment):

```bash
cd stravaV10/pca10056/s340/armgcc
make                    # needs Makefile.local (gitignored) defining SDK_ROOT
make flash_softdevice   # or: make dfu_softdevice
make flash              # or: make dfu
```

Host-side test/simulation build (business logic + mocked hardware, runs on
Linux, no target needed -- what proves the ported logic was hardware-decoupled
to begin with):

```bash
cd stravaV10
mkdir build && cd build
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug ..
make
./StravaV10
```

## Offline tools (`stravaV11_fw/tools/`)

```bash
# GPX route -> C array for gps_sim_demo.c's replay (SIM START)
python3 tools/gpx_to_c.py <input.gpx> <n_points> src/gps_sim_route.h

# OSM PBF/XML extract -> on-device map tile files
python3 tools/osm_to_tiles.py <input.osm[.pbf]> <output_dir> \
  --bbox <min_lon> <min_lat> <max_lon> <max_lat>

# Inspect one generated tile file
python3 tools/osm_to_tiles.py --dump <tile.bin>

# Embed a directory of tile files into a C header (only needed for the
# small flash-embedded demo tiles; bulk coverage goes onto the SD card
# directly via USB MSC instead, see above)
python3 tools/tile_to_c.py <tiles_dir> <output.h> --var-prefix <name>
```

Fetching real OSM data for `osm_to_tiles.py` via Overpass -- use the
`(._;>;); out body;` idiom, **not** `out body; >; out skel qt;`. The latter
emits ways before the nodes they reference, which breaks osmium's
single-pass node-location resolution and silently drops every way (0 kept):

```bash
curl -s "https://overpass-api.de/api/interpreter" --data-urlencode \
  'data=[out:xml][timeout:60];(way["highway"](<south>,<west>,<north>,<east>););(._;>;);out body;' \
  -o extract.osm
```

For anything beyond a small area, prefer a real regional extract (e.g. from
`download.geofabrik.de`, which may need fetching from a machine other than
this one) over a large live Overpass query -- Overpass's own usage policy
discourages bulk area queries, and a >2M-way query is genuinely heavy on a
shared free service.
