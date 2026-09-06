# Maps feasibility study

Date: 2026-09-06. Scope: whether/how to add on-device map rendering to the
`stravaV11` port (nRF52840, LS027 Sharp memory LCD, custom PCB — see
`CLAUDE.md`). This is a feasibility study, not a design doc — no
implementation exists yet.

## Bottom line

Feasible, but as a genuinely new subsystem, not a port of anything —
this codebase has no map-rendering code at all, only a dead-code
zoom-scaling stub and a phone-driven turn-icon feature that's already out
of scope for this port. The right architecture is **on-device vector
street rendering from an OSM-derived custom format, streamed off the SD
card**, not raster tiles. It's comparable in scope to everything else
done in this port combined so far — a multi-phase project of its own, not
a quick add-on.

## What's actually there today

Grounded in the stravaV10 codebase, not assumed:

- **`libraries/komoot/komoot_nav.c`/`komoot_icons.h`**: not a map renderer
  at all — it's a lookup table returning one of a handful of pre-baked
  110x110px 1bpp turn-arrow bitmaps (straight/left/right/roundabout/finish),
  driven by turn cues **pushed from the Komoot phone app over BLE**
  (`ble_komoot_c`). That service is explicitly out of scope for this port
  (Phase 5 decision, see `CLAUDE.md`) — so even the one navigation-adjacent
  feature stravaV10 ever had depended on a phone doing the actual
  map/routing work, not the device itself.
- **`source/display/Zoom.h`/`.cpp`** (already ported to `stravaV11_app` in
  Phase 1): computes a lat/lon degree-span from a "zoom level" integer for
  a small screen widget. Grepping the entire stravaV10 tree, it's
  referenced by **nothing else** — no `Vue*` screen ever instantiates or
  calls it. It's a scaffold that was built and never wired up.
  `task_manager_wrapper.h` still has a dead `ComputeZoom` task-ID constant
  for it.
- **`source/display/SegmentManager`**: tracks/scores nearby Strava segments
  (start/end proximity, leaderboard-adjacent state) — not rendering.
- **`source/routes/*`** (`Parcours`, `Segment`, `Points`, `ListePoints`):
  route/segment polyline geometry for progress and pace math — a data
  model, not a basemap.

So "add maps" means designing and building this from nothing, using only
the graphics primitives already ported (`AdafruitGFX`/`ZephyrGFX`, Phase 7)
as a foundation.

## Hardware budget

From this port's own validated numbers (see `CLAUDE.md`), not spec-sheet
guesses:

| Resource | Real capacity | Notes |
|---|---|---|
| RAM | 256KB total, ~170KB free at idle | Current smoke-test build already uses ~84KB (BLE+ANT+display+etc. concurrently). A map subsystem competes with everything else running simultaneously. |
| Internal flash | 1MB, ~499KB per MCUboot slot | Effectively zero spare room — app code alone already uses a meaningful fraction. |
| External QSPI NOR | 16MB (real board's MT25QL128), memory-mapped mode validated | Too small for a real geographic area of street data at any reasonable detail — maybe a city-sized crop, not a region. |
| SD card | Real ~7.4GB card already validated (raw sector r/w) | The only store with realistic capacity for a meaningful map area. **FAT filesystem itself is unproven** — only raw `disk_access` sectors have been tested; `CONFIG_FAT_FILESYSTEM_ELM` was descoped for the QSPI flash specifically due to a Partition Manager quirk (Phase 4), but that issue was about flash-partition affiliation metadata, not `disk_access` in general — the SD card almost certainly doesn't hit the same wall, but this hasn't actually been tried. |
| Display | LS027, 400x240, 1bpp monochrome | No greyscale/anti-aliasing — map rendering means hard black/white lines only. |
| CPU | Cortex-M4, ~64MHz | Comfortable for line-drawing math at GPS update rate (~1Hz); not comfortable for image decode/dithering. |

## Three architectures, compared

**1. Raster tiles (PNG/JPEG map images, like a phone map app)** — **not
feasible here.** Even heavily downsampled 1bpp dithered tiles would need
real-time decode + dithering on a CPU with no meaningful decode
acceleration, and the RAM budget can't hold a decoded tile buffer plus
everything else already running. This is the wrong model for this class
of MCU — no production bike computer in this hardware tier does it this
way either.

**2. Vector street maps (simplified line/polygon geometry, rendered
live)** — **the right approach, and the proven approach.** This is
exactly what Garmin Edge-class devices and most dedicated GPS bike
computers actually do: a compact pre-processed vector dataset (road
centerlines classified by type, place labels, POI points), rendered as
line segments at runtime scaled to the current zoom. It reuses
`ZephyrGFX`'s existing `drawLine()`/`drawPixel()` primitives directly —
no new rendering primitives needed, only a new data source feeding them.

**3. Route-line-only, no basemap (just position + planned route, like
`Parcours`/`Segment` already partially support)** — **already the closest
thing to "done"**, but it isn't really "maps" — no street context, just
the pre-loaded route polyline. Worth calling out as the cheap fallback
if full maps prove too large a lift.

## Recommended design: vector maps, SD-card-backed

**Offline pipeline (host-side, same pattern as `ncs/stravaV11_fw/tools/gpx_to_c.py`)**:
take an OSM extract (via `osmium`/`osmconvert`) for the riding region,
filter to road/path classes relevant to cycling, simplify polylines
(Douglas-Peucker) per zoom tier, quantize coordinates to fixed-point
relative to a tile origin, and emit a **custom compact binary tile
format** — not a general-purpose format like Mapsforge/MVT, both of which
assume a decoder with far more RAM/CPU headroom than this chip has.
Something like: a fixed-size tile grid (e.g., ~0.02 degree per tile), each
tile a flat array of polylines with a road-class byte and int16 quantized
points, written to the SD card as one file per tile or a simple indexed
blob.

**On-device runtime**:

- Load only tiles within current view + a margin (a handful of tiles
  resident at once, discarding out-of-view ones) — never load a whole
  region into RAM.
- Reproject the loaded polylines to screen X/Y using the same kind of
  local flat-earth math `Zoom::computeZoom()` already sketches (that dead
  code is actually a reasonable starting point for the projection math,
  even though it was never wired up).
- Draw via existing `ZephyrGFX::drawLine()` calls, redrawn at GPS update
  rate (~1Hz) — not a real-time refresh, well within CPU budget.
- Center/pan driven by `Locator::getPosition()`, already flowing correctly
  (validated with both real and simulated GPS fixes).

**Rough storage math** (ballpark, not a precise spec): a simplified
cycling-relevant road network (major+minor roads, tracks, no full building/
POI detail) typically runs somewhere in the low single-digit MB per
1000km² at this simplification level. A regional riding area — a few
thousand km² — likely lands in the tens-of-MB range, comfortably inside
the 7.4GB SD card and nowhere close to fitting in 16MB QSPI NOR. This is
the concrete reason SD (not QSPI) has to be the map store.

## Offline conversion tool

`ncs/stravaV11_fw/tools/osm_to_tiles.py` (2026-09-06) implements the
pipeline described above. Full format spec and usage are in the script's
own docstring; summary:

- **Input**: an OSM PBF extract, parsed with the `osmium` package
  (`pip install osmium` — that's the real PyPI name; "pyosmium" is just
  the upstream project's name, not the package name). An optional
  `--bbox` further filters ways.
- **Filtering**: `highway=*` values relevant to cycling only (motorway
  through footway/cycleway/track, 6 road classes by importance), mapped
  to a single road-class byte; anything else (buildings, waterways,
  landuse, unmapped highway values) is dropped. No polygon/fill
  rendering is in scope (matches the "vector lines only" rendering
  approach — `ZephyrGFX` has no fill-polygon support and 1bpp/400x240
  wouldn't benefit from it at road-navigation zoom anyway).
- **Simplification**: Douglas-Peucker per way (pure Python, no extra
  dependency), default epsilon ~16.7m, applied once on the full way
  *before* tiling.
- **Tiling**: a flat 0.02-degree grid (~2.2km latitude span). A way
  crossing a tile boundary is split into per-tile segments, with the
  boundary point duplicated into both tiles so lines still connect
  visually when tiles are rendered side by side.
- **On-device format** (v2, magic `SV11MAP2`): fixed-layout little-endian
  binary (matches the nRF52840's byte order — no swapping needed), points
  quantized to int16 lat/lon in 1e-6 degree units relative to each tile's
  SW corner (~11cm resolution, an order of magnitude finer than the
  simplification tolerance, so quantization itself isn't a meaningful
  error source), plus an int16 altitude in whole metres per point (see
  "Altitude" below). One flat file per tile, named
  `tile_<lat_idx>_<lon_idx>.bin` — not 8.3-constrained now that
  `CONFIG_FS_FATFS_LFN` is enabled. The same script has a
  `--dump <tile.bin>` mode to inspect a tile's contents (parses back to
  lat/lon/alt and asserts the byte count matches exactly), useful for
  verifying on-device output later too.

**Validated against a synthetic test PBF** (built with `osmium.SimpleWriter`,
no network/real-extract dependency needed to test the pipeline logic
itself — matches this port's "logic before hardware" discipline, applied
here to "logic before a real map download"): 5 ways covering every
control-flow path — a way crossing a tile boundary, a way with one real
geometric deviation (correctly keeps the geometrically-necessary points
around it — standard recursive Douglas-Peucker behavior, not a bug, even
though it keeps more points than a naive "collapse near-collinear runs"
intuition expects), a way of genuinely collinear points (correctly
collapses 6 points to 2, unambiguously confirming simplification works),
an unmapped `highway=steps` way, and a way with no `highway` tag at all.
Result: `Ways seen: 5, kept: 3` (both drops correct), tile-boundary
duplication confirmed byte-for-byte (the crossing point appears as the
last point of one tile's polyline and the first point of the
neighboring tile's), and every output tile round-trips through
`--dump` with byte-exact structural parsing (no overrun/misalignment).
One real bug was caught and fixed during this: `split_into_tiles()` can
return multiple segments for the same tile (a way can leave and re-enter
a tile), which the first version of `way()` didn't handle correctly.

**Not yet done**: running this against a real Geofabrik/OSM extract of an
actual riding area (needs the user to pick a region and download it —
not attempted here, no real-world map data was fetched); a
whole-country-scale run would also want the `OSM_Extract`-style
folder-grouping the library survey documents, not implemented here (a
flat directory is fine for a single-region test extract).

The synthetic-test-PBF generator itself is now a kept tool too, not just
a one-off scratch script: `tools/make_test_pbf.py` — needed again for
the altitude format change below, and will be needed again if the
format changes further.

## Altitude

The on-device format didn't originally carry elevation. Added per-point
in format v2 (magic bumped `SV11MAP1` → `SV11MAP2` — a real, deliberate
break: v1 tiles are not readable by v2 tooling or the v2 parser, since
nothing has ever shipped real map data yet, there was no reason to
support both). Real elevation for arbitrary road/path points isn't in
OSM way data itself — only some tagged nodes (peaks, passes, and
similar) carry an `ele` tag; getting real elevation *everywhere* would
need a separate DEM (elevation raster) source sampled per point, a
materially bigger addition (a new dependency, a DEM file to obtain).
**Decided against that for now** — `osm_to_tiles.py` sources altitude
from OSM node `ele` tags where present (real data, zero new
dependencies) and writes `ALT_UNKNOWN_M` (int16 `-32768`) where absent,
which is most ordinary points until a DEM step is added later.

Every layer was updated together: the wire format (`POINT_FMT` gained a
third `i16` field), `map_tile.c`'s parser (`map_tile_point_at()` gained
an `*alt` out-parameter, passing `MAP_TILE_ALT_UNKNOWN_M` through as-is —
deliberately not `NAN`, so callers do a plain sentinel comparison rather
than an `isnan()` check, and so the existing fixed-point `printk()`
formatting convention this port uses everywhere else doesn't have to
special-case a non-finite float), and `test_tile_data.h` (regenerated
from a real tool run, not hand-edited — the test PBF's way B now carries
a realistic mix: one node with a real integer `ele`, one with a real
fractional `ele` given as a string ["123.5", rounds to 124 — OSM tag
values are always strings], one with a garbage non-numeric `ele`, and
two nodes with no `ele` tag at all, so the test exercises all three real
cases in one polyline rather than only the all-present or all-absent
extremes).

**Validated on `native_sim`** (`stravaV11_app`'s smoke test, same
tolerance-based comparison as lat/lon) **and on real hardware**
(`stravaV11_fw`'s `map_demo.c`, same seed-then-load-through-the-generic-
path methodology as before): both report all 5 points' altitude exactly
matching the offline tool's own byte-level decode —
`45m, unknown, 124m, unknown, unknown` — with zero regressions in
lat/lon decoding or any other subsystem running in the same boot.

## On-device tile parser

`stravaV11_app/lib/source/maps/map_tile.{h,c}` (2026-09-06), validated on
`native_sim`. Deliberately not a bounded-array-copy decoder that expands
a whole tile into a float array — RAM is the tightest constraint
identified in this study, so it's a stateful iterator directly over the
caller-owned buffer (`map_tile_iter_init()`/`map_tile_iter_next()`),
decoding one point to float lat/lon/altitude at a time via `map_tile_point_at()`,
matching this project's existing wire-format-parsing convention
(`bt_cp_client.c`'s `sys_get_le16()`/`sys_get_le32()`, not a struct
overlay — portable across alignment/padding rules). Also implements
`map_tile_name_for(lat, lon, name_out)`: the lat/lon-to-filename half of
"loading" a tile, using `MAP_TILE_DEG` kept in sync with
`osm_to_tiles.py`'s `TILE_DEG` and double-precision internally to avoid
a float32-vs-Python-float64 rounding mismatch right at a tile boundary.

**Validated against a real tool-generated tile, not hand-crafted bytes**:
`stravaV11_app/lib/source/maps/test_tile_data.h` embeds the actual bytes
of `tile_2_2.bin` from the offline tool's own synthetic-PBF test run
(chosen specifically for its non-zero tile origin, so the test exercises
`map_tile_point_at()`'s origin+delta addition, which an origin-(0,0)
tile wouldn't). `stravaV11_app/src/main.cpp`'s smoke test decodes it and
checks every value against expectations hand-computed with Python's
`struct` module (not just trusting the C parser to agree with itself):
polyline count, road class, all 5 points' lat/lon, clean end-of-iteration
after the last polyline, `map_tile_name_for(0.05, 0.05)` resolving to the
same `tile_2_2.bin` the point actually lives in, and two error paths (bad
magic, a buffer too short to hold the header). **All pass** on
`native_sim` (`west build -b native_sim/native/64 stravaV11_app`, then
run `zephyr.exe`).

**The remaining `fs_open()`/`fs_read()` glue is now also done, and
validated on real hardware.** `map_tile.{h,c}` and `test_tile_data.h`
were copied unmodified into `stravaV11_fw` (same "copy, don't symlink"
precedent as `Locator.cpp`), and a new `stravaV11_fw/src/map_demo.c`
wires them to the real SD card: it seeds the card with the same real
tool-generated `tile_2_2.bin` bytes, then loads it back using *only* the
generic loading path a real GPS-driven lookup would use —
`map_tile_name_for()` to compute the filename, `fs_open()`/`fs_read()`
by that name, then `map_tile_iter` parsing — not any special knowledge
of the buffer the same demo just wrote. On the real board (byte counts below are the original v1, lat/lon-only
format — the "Altitude" section further down covers the v2 format
change and its own, separately validated, byte counts):
`map_demo: fs_mount("/SD:") -> 0`, `seeded /SD:/tile_2_2.bin (44 bytes)`,
`fs_read() -> 44 bytes`, and all 5 points decoded exactly matching the
same values already verified on `native_sim` — `lat=0.050000
lon=0.050000` through `lat=0.050000 lon=0.060000` — with every other
Phase 2-11 subsystem still running cleanly in the same boot. The on-device
loading pipeline is now fully proven end to end; only real map data (an
actual OSM extract of a real riding area) is missing, not any remaining
plumbing.

## Real risks and open questions

1. ~~**SD FAT filesystem is unproven.**~~ **Resolved 2026-09-06, validated
   on real hardware.** `CONFIG_FAT_FILESYSTEM_ELM` mounts cleanly on the
   real SD card (`fs_mount("/SD:") -> 0`), and a real file create/write/
   read/verify round trip succeeds (`stravaV11_fw/src/main.c`'s
   `sd_fat_demo()`, see `CLAUDE.md`). One real gotcha hit along the way:
   filenames must be 8.3-format unless `CONFIG_FS_FATFS_LFN` is enabled (a
   name like `stravav11_test.txt` fails `fs_open()` with `-ENOENT`, not an
   obviously-named length error). **`CONFIG_FS_FATFS_LFN` is now enabled**
   (default BSS working-buffer mode — fine, no concurrent FS access from
   multiple threads yet) and re-validated: the same long filename now
   creates/writes/reads back correctly, listed by its full name. Future
   map-tile filenames aren't 8.3-constrained.
2. **RAM is the tightest constraint**, and it's shared with ANT+/BLE/
   display/everything else already running concurrently — the tile cache
   size has to be sized empirically against real peak usage, not assumed.
3. **No existing parser for anything map-shaped** — the on-device
   tile-loading/parsing code is new, unlike most of this port which has
   been "carry the logic, replace the plumbing." (See the library survey
   below for whether any of this can be reused rather than written from
   scratch.)
4. ~~**LS027 SPI throughput for panning smoothness** hasn't been
   profiled.~~ **Resolved 2026-09-06, measured on real hardware.** A real
   ~745-polyline tile (1570 points): ~127-129ms render + ~79.5ms SPI push
   = ~207ms total; a smaller real tile (1087 points): ~91ms render +
   ~79ms push = ~170ms. Push time is roughly constant (a fixed-size full
   frame transfer); render time scales with point count. Comfortably
   within the current 1000ms (1Hz, GPS-update-rate) redraw budget —
   interactive pan/zoom at a faster refresh rate would need re-checking
   against this same data, but the current design has real headroom.
5. **No design work yet for how zoom/pan is actually triggered** — buttons
   exist on the real board (P0.14/P0.13/P0.11) but no input-handling code
   has been ported (`button.h`/`Notif.h` are explicitly still un-ported
   per Phase 7).
6. ~~**A genuine power-on reset was observed** a few minutes into the
   SD-card-backed version of live map redraws (concurrent with the full
   subsystem set).~~ **Resolved 2026-09-06, high confidence.** See "Map
   rendering" below for the diagnosis (`POWER.RESETREAS`-confirmed, not
   a software crash) and mitigation (a self-healing power latch,
   re-asserted every 5s rather than only once at boot). Re-tested with
   `main()` fully restored plus a new, deliberately heavier/faster SD
   stress test (32KB every 500ms vs. the original ~10KB/1000ms) running
   concurrently with everything else, including live map redraws: 461
   cycles over ~400 seconds, zero corruption, zero reboots — longer than
   both prior crash times. Not an absolute guarantee, but real evidence
   under a harder test than the original failure case.
7. ~~**Two modules (`map_screen_demo`, `sd_stress_demo`) each held their
   own persistent `/SD:` mount.**~~ **Resolved 2026-09-06.** Zephyr's fs
   layer allows only one mount per path at a time (`fs_mount()` returns
   `-EBUSY` on a second attempt) — whichever module mounted second failed
   on every boot, silently (the one failing `printk()` was lost in a
   congested boot-time logging burst), making it look like that module
   had simply stopped working. Fixed by having both mount/operate/unmount
   transiently per cycle instead of holding a persistent mount — the same
   pattern `sd_fat_demo()`/the original `map_demo()` already used safely.
   A general lesson for any future module that needs `/SD:`: don't assume
   a persistent mount is free just because one module already has one.

## Suggested phased plan

1. ~~Validate `CONFIG_FAT_FILESYSTEM_ELM` actually mounts on the SD
   card.~~ **Done** — see risk #1 above.
2. ~~Build the offline OSM-to-binary-tile conversion tool.~~ **Done** —
   see "Offline conversion tool" below.
3. ~~On-device: a minimal parser, tested on `native_sim` first; then the
   real `fs_open()`/`fs_read()` loading glue on hardware.~~ **Done** —
   see "On-device tile parser" below. The loading pipeline (filename
   resolution, real SD-card file read, decode) is fully validated
   end-to-end on real hardware; only real map data is still missing.
4. ~~Wire the (currently dead) `Zoom` projection math to real rendering
   via `ZephyrGFX`, validate against `native_sim` with a fixed test
   tile.~~ **Done, and validated on real hardware too** — see "Map
   rendering" below. Zoom is a fixed constant for now (one define,
   `MAP_RENDER_ZOOM_LEVEL`), not interactive — the user's explicit choice,
   since no button/pan/zoom input handling exists yet (`button.h`/
   `Notif.h` are still un-ported, per Phase 7).
5. Hardware bring-up on the real board: ~~real map data (an actual OSM
   extract of a riding area, not the synthetic test tile), real
   GPS-driven panning/re-centering~~ **done and confirmed on the physical
   panel** — see "Map rendering" below ("I could recognize streets").
   ~~Resolving the power-on-reset question that surfaced along the
   way~~ **done, high confidence** — see risk #6 above. ~~Re-integrating
   this with SD-card-backed tile storage specifically (current
   validation embeds tiles directly in flash, which doesn't scale to a
   real riding area's worth of map data)~~ **done** — see "Map rendering"
   below; `map_screen_demo` now does a real `fs_open()`/`fs_read()` per
   redraw, validated over 202 real tile loads with zero corruption
   alongside heavy concurrent SD stress. Still open: buttons for
   interactive zoom (new input-handling work, not started).

## Map rendering

`stravaV11_app/lib/source/maps/map_render.{h,cpp}` (2026-09-06), validated
on `native_sim` and real hardware. Projects and draws a parsed
`map_tile.c` buffer onto a `ZephyrGFX` canvas, centered at a caller-given
lat/lon, using `Zoom::computeZoom()` (`source/display/Zoom.{h,cpp}`,
ported in Phase 1, never called by anything until now) for the
lat/lon-degree-span-to-screen-pixel math. **Zoom is a single constant**
per the user's explicit direction — `MAP_RENDER_ZOOM_LEVEL` in
`map_render.h`, the one value to change, not a runtime/button-driven
level — which only needed one small, in-character addition to `Zoom`
itself (`setZoomLevel()`, since the class previously only exposed
`increaseZoom()`/`decreaseZoom()`/`resetZoom()`, no way to jump straight
to an arbitrary level). Longitude maps directly to screen x; latitude
maps to screen y inverted (north is "up", but pixel y increases
downward). **Update**: originally relied solely on `Adafruit_GFX`'s own
bounds-checked `drawLine()`/`writePixel()` and did no clamping of its
own — real map data broke that assumption (see below): projected
coordinates now get clamped to a generous margin before the
`float`→`int16_t` cast (undefined behavior otherwise for an extreme
value), plus a Cohen-Sutherland-style trivial-reject test skips segments
whose endpoints share an out-of-bounds side, bounding the work done for
a tile whose content mostly falls outside the current view.

**Validated on `native_sim`** (`stravaV11_app`'s smoke test, same real
tool-generated `tile_2_2.bin` fixture as the parser tests): renders onto
its own `ZephyrGFX` instance, `fillScreen(1)` (white) then black
(color 0) lines, checked via the same before/after pixel-count rigor as
every other `ZephyrGFX` smoke test in this port — `5 points drawn`
(matches the tile's point count) and the set-pixel count *decreased*
(white pixels turned black), the opposite direction from the
black-background `ZephyrGFX` test elsewhere in the same file, which
tripped up the first version of this check (a real, caught-and-fixed
test bug — the render itself was correct the whole time, `96000 → 95800`
consistently, just the pass/fail direction was backwards).

**Validated on real hardware**: `stravaV11_fw/src/gfx_demo.cpp` gained
`gfx_demo_show_map()`, called from `map_demo.c` right after it loads and
parses the seeded tile from the SD card — reusing the exact buffer
`fs_read()` just filled, no re-derivation. `main()` was reordered so
`map_demo()` runs *last*, immediately before `sensor_screen_demo_start()`
hands the display over to the periodic LIVE DATA screen for good — the
same brief, real visible window `gfx_demo()`'s own static message
already got, rather than being invisibly overwritten within
milliseconds by `display_demo()`/`gfx_demo()` if drawn earlier (all
three share the same physical screen; there's no menu/mode-switching in
this port yet). Confirmed via a fresh-flash RTT capture: `gfx_demo:
95880 pixels set, display_write() -> 0` (of 96000 total —120 pixels
drawn black, matching a real, non-trivial render, not a blank push) then
`gfx_demo: map render, 5 points drawn`, staying on screen for roughly a
second (the LIVE DATA screen's own first redraw, `91156` pixels, is the
next `gfx_demo:` line) before being replaced, with zero regressions
anywhere else in the same boot. Not yet independently confirmed by the
user looking at the physical panel — the log/pixel-count evidence is
solid, but (per this port's own established standard for display
features) only eyes on real glass fully closes that loop.

**Update — real OSM map data rendered and confirmed on the physical
panel: "I could recognize streets."** First time this port has shown
anything but a synthetic test pattern on real glass. Getting here needed
a real OSM extract (`curl` against the public Overpass API for the bbox
covering `gps_sim_route.h`'s actual Rotterdam ride — 2889 real ways
seen, 1265 kept, converted via `osm_to_tiles.py` into 2 tiles,
`tile_2596_223.bin`/`tile_2596_224.bin`, confirming the route really
does cross a tile boundary as predicted from its known lat/lon span),
embedded via a new kept tool `tools/tile_to_c.py` (generalizes the
`test_tile_data.h` pattern — a directory of tile files to a C array +
`struct embedded_tile { name, data, len }` lookup table — into
`lib/source/maps/real_route_tiles.h`) — and, the actual blocker, root
causing why the live map screen never rendered at all despite building
and running cleanly.

**Root cause: `Locator::getPosition()` is a single-consumer read.** It
calls `gps_loc.clearIsUpdated()` as a side effect. `gps_sim_demo.c`'s own
`replay_work_handler()` calls `gps_demo_inject_location()` then
immediately `gps_demo_report()` (which calls `getPosition()`) back to
back in the same function, winning the race against any other,
independently-scheduled poller essentially every time.
`gps_demo_get_position()`/`gps_demo_get_altitude()` both also called
`getPosition()`, and so never once saw a fix, despite real fixes flowing
continuously the whole time — confirmed by direct contradiction in one
capture's raw log: `Locator update source: 3` (a real fix, consumed by
`gps_demo_report()`) appearing constantly, right alongside
`map_screen: no GPS fix yet` on every single cycle. **Fixed by reading
`gps_loc.data` directly** (a public `Sensor<T>` member) instead of going
through `getPosition()`, using `getAge()` (also side-effect-free) for
staleness (`< 5000ms`) instead of `isUpdated()`, which would just latch
permanently true after the first-ever fix. This is a real,
generally-applicable bug fix, not maps-specific — any future code
reading Locator's position from more than one place would hit it too.

**A second, separate issue surfaced while isolating this**: a genuine
power-on reset (`POWER.RESETREAS` read `0x00000000` — no warm-reset-
reason bits set — immediately after the reboot, ruling out a software
crash) a few minutes into the SD-card-backed version of this test (real
`fs_write()`/`fs_open()`/`fs_read()` every redraw cycle, concurrent with
QSPI/ANT+/BLE/display all at once; confirmed on stable USB power, ruling
out battery drain). Mitigated with a self-healing power latch
(`stc3100_power_latch_hold()` was previously only ever called once, at
boot — now also re-asserted every 5 seconds) — a defensive mitigation,
not a root-caused fix for a specific disturbance mechanism.

**Isolated methodically**: rewrote `map_screen_demo.c` to render
directly from `real_route_tiles.h` with no SD card or filesystem
involved at all, and stripped `main()` down to only what
`map_screen_demo`/`gps_sim_demo` need, cleanly separating "does rendering
work at all" from "does the board lose power" as two previously-
conflated questions.

**Fully validated on real hardware, 5-minute continuous run, zero
crashes**: 146 successful renders, both real tiles rendered correctly as
the simulated ride crossed the tile boundary mid-route
(`tile_2596_223.bin`, 1570 points, ~127-129ms render + ~79.5ms push =
~207ms/redraw; then `tile_2596_224.bin`, 1087 points, ~91ms render +
~79ms push = ~170ms/redraw — push time roughly constant, matching a
fixed-size SPI frame transfer; render time scales with point count),
comfortably within the 1000ms redraw budget. Zero crashes across the
whole run — longer than either prior crash (~2:36 and ~4:55 with the
SD-card version).

**Update — `main()` fully restored, plus a new dedicated heavy SD-card
stress test (`sd_stress_demo.{h,c}`): the power-loss question is now
resolved with high confidence.** Every subsystem (`led_button_demo()`
through `usb_demo_start()`) is back, unchanged, just previously left
uncalled during isolation. The new stress test mounts its own `/SD:`
volume and, every 500ms, writes then reads back a 32KB buffer —
deliberately heavier and faster than the original map-loading cadence
that first showed the power loss (~10KB/1000ms) — specifically to
re-test the actual originally-crashing configuration: the full subsystem
set running concurrently with real, sustained SD-card I/O, live map
redraws included. **Result, ~400-second (6.7 minute) continuous run**:
461 stress cycles, every one `MATCH` (zero corruption), 185 successful
map renders, and zero reboots/crashes anywhere — longer than both prior
crash times, under a harder test than the original failure case. The
periodic STC3100 power-latch refresh (re-asserted every 5s, not just
once at boot) is the leading explanation, though not root-caused to a
specific disturbance mechanism.

**Not yet done** *(at the time of that update)*: re-integrating SD-card-backed
*tile loading specifically* (the map screen itself still rendered from
tiles embedded in flash, `real_route_tiles.h` — proven fine standalone
above, but not yet re-combined with `map_screen_demo` reading real tiles
from the SD card the way `sd_stress_demo` proves the card itself can now
handle) — since resolved, see below. Buttons for interactive zoom
(`button.h`/`Notif.h` still un-ported, per Phase 7) remain open.

**Update — `map_screen_demo` wired back to the real SD-card round trip, completing this feature's original design intent.** Rewritten to seed `real_route_tiles.h`'s tile bytes onto `/SD:` once at start, then do a genuine `fs_open()`/`fs_read()` off the card each redraw cycle by the filename `map_tile_name_for()` computes — the direct-from-flash-array shortcut used above for isolation is no longer the live code path.

First attempt surfaced one more real bug: `map_screen_demo.c` and `sd_stress_demo.c` each independently held a **persistent** mount on `"/SD:"` — fine in isolation, but Zephyr's fs layer allows only one mount per path at a time, so whichever module's `fs_mount()` ran second (`sd_stress_demo_start()` runs first in `main()`) failed on every boot, silently, because the one failing `printk()` was lost in the same kind of congested boot-time logging burst this port hit before (Phase 11's GPS RX-dump). `map_screen_demo` looked totally silent for an entire 300-second capture as a result. Fixed by having both mount, do one operation, and unmount again every cycle instead — the transient pattern `sd_fat_demo()`/the original one-shot `map_demo()` already used safely; the two-persistent-mounts design was the actual anomaly.

**Fully validated on real hardware, ~300-second run, full subsystem set plus `sd_stress_demo`'s heavy concurrent I/O**: 202 successful `map_screen` `fs_open()`/`fs_read()` tile loads, correctly split across both real tiles as the route crosses the tile boundary (`tile_2596_223.bin` ×130, `tile_2596_224.bin` ×72), 202 matching renders, 276 `sd_stress` cycles all `MATCH`, zero crashes. The SD-card-backed map path this feature always needed for real-world coverage beyond flash capacity is now proven end-to-end.

## On-device library survey

Researched online (2026-09-06) for anything usable as-is, rather than
building the custom format/parser from scratch. Conclusion: **nothing
found is directly reusable, but real precedent strongly confirms the
custom-format approach is the standard solution at this hardware tier**,
not a shortcut being taken to avoid an existing one.

### Nothing Zephyr/nRF52-specific exists

Searched directly for a Zephyr RTOS or nRF52-targeted offline-map project;
found none. This is genuinely new ground for this RTOS/SoC combination,
confirming the "build it" framing above rather than "port it."

### General-purpose vector tile libraries: real, but not a fit

- **[vtzero](https://github.com/mapbox/vtzero)** (Mapbox) — a
  "minimalist" C++14 Mapbox Vector Tile decoder/encoder, built on top of
  **[protozero](https://github.com/mapbox/protozero)** for the underlying
  protobuf parsing. It's the closest thing to a real lightweight MVT
  parser that exists. But its docs, build system (CMake, `ctest`), and
  total absence of any embedded/microcontroller-suitability claims all
  point at a desktop/server target, not an MCU. More importantly: **it
  solves a problem we don't have.** MVT/protobuf's generality (varint
  encoding, arbitrary tag/value schemas, arbitrary layer structure) exists
  to let *arbitrary* producers and consumers interoperate — irrelevant
  here, since the offline converter and the on-device parser are both
  code we write and fully control. A fixed-layout binary format (flat
  arrays of quantized int16 points, a road-class byte, no schema
  negotiation) is strictly simpler to parse in bounded RAM than protobuf's
  general varint/tag decoding, at zero loss of capability for this use
  case. Adopting vtzero would mean carrying its C++14/protozero dependency
  for a decode problem simpler than what it's built to solve.
- **[libosmscout](https://github.com/Framstag/libosmscout)** — a real,
  mature C++ offline OSM rendering/routing library. No published RAM
  floor was found, but its target platforms (desktop/Android/iOS) and
  feature scope (full routing engine, styled rendering, search) place it
  well above this project's budget; not evaluated further as a serious
  candidate for a 256KB-RAM MCU.
- **Mapsforge** — Java-only; not applicable to a C/C++ Zephyr app at all.

### Real precedent: hobbyist bike-computer/GPS-navigator projects doing exactly this

These are the actually relevant data points — independent projects on
comparable (mostly ESP32-class) hardware, solving this exact problem:

- **[bike-computer-32](https://github.com/lspr98/bike-computer-32)**
  (lspr98) — an ESP32-**C3** (RISC-V, no PSRAM, a genuinely comparable RAM
  tier to the nRF52840's 256KB) bike computer with OSM offline maps and
  GPX rendering. Uses a **custom binary format** (`map.bin`, built by a
  bespoke C++ conversion tool, no existing library), stored on a FAT32 SD
  card, and the README claims **60 FPS rendering** in typical scenes. This
  is the strongest single data point in this survey: real vector-map
  rendering, on real MCU hardware in roughly our class, using a
  rolled-your-own format — not an off-the-shelf library, and not
  underpowered for the task.
- **[IceNav-v3](https://github.com/jgauchia/IceNav-v3)** (jgauchia) —
  ESP32-S3 (more RAM/PSRAM than our target) GPS navigator explicitly
  offering both raster tiles *and* a smaller **custom `.bin` vector
  format**, generated by a companion tool,
  **[Tile-Generator](https://github.com/jgauchia/Tile-Generator)**. Uses
  LVGL + LovyanGFX for rendering — both color-TFT-oriented, not
  applicable to our 1bpp LS027, but the two-repo split (device firmware +
  separate host-side generator) matches the `gpx_to_c.py`-style pattern
  already established in this port.
- **[ESP32_GPS](https://github.com/aresta/ESP32_GPS)** (aresta) — targets
  ESP32-S3 with PSRAM (more headroom than our target). Its companion
  offline-conversion tool,
  **[OSM_Extract](https://github.com/aresta/OSM_Extract)**, is the most
  concretely useful reference of the three for *our* offline pipeline
  even though the firmware itself doesn't run on comparable hardware:
  Python, using **osmium** to clip a Geofabrik PBF extract to a
  GeoJSON-defined bounding box, then a YAML-configured feature filter
  (road/path types, colors, line widths) emits a custom text-based
  `.fmp` format, tiled as ~4x4km files grouped into ~64x64km folders
  (16x16 file grids). This confirms two design choices independently:
  **osmium** as the right host-side PBF-processing library (Python
  bindings: `pyosmium`), and **grid-tiled custom files on SD** as a
  working real-world tiling granularity, not an arbitrary guess.

### Net conclusion for the library question

No existing library should be adopted for the on-device parser/renderer —
write a small fixed-layout binary format and a matching Zephyr-native
parser (a few hundred lines, not a new dependency). For the *offline*
host-side conversion tool, **osmium** (via `pyosmium`, matching this
port's existing Python-tooling convention from `gpx_to_c.py`) is the
correct, proven choice for PBF extraction/clipping — reuse that rather
than writing raw PBF parsing from scratch. `vtzero`/`protozero` are worth
knowing about but not adopting: they solve interoperability generality
this project doesn't need.

Sources consulted:
- [bike-computer-32](https://github.com/lspr98/bike-computer-32)
- [IceNav-v3](https://github.com/jgauchia/IceNav-v3)
- [Tile-Generator](https://github.com/jgauchia/Tile-Generator)
- [ESP32_GPS](https://github.com/aresta/ESP32_GPS)
- [OSM_Extract](https://github.com/aresta/OSM_Extract)
- [vtzero](https://github.com/mapbox/vtzero)
- [protozero](https://github.com/mapbox/protozero)
- [libosmscout](https://github.com/Framstag/libosmscout)
