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

## Real risks and open questions

1. ~~**SD FAT filesystem is unproven.**~~ **Resolved 2026-09-06, validated
   on real hardware.** `CONFIG_FAT_FILESYSTEM_ELM` mounts cleanly on the
   real SD card (`fs_mount("/SD:") -> 0`), and a real file create/write/
   read/verify round trip succeeds (`stravaV11_fw/src/main.c`'s
   `sd_fat_demo()`, see `CLAUDE.md`). One real gotcha hit along the way,
   worth remembering for the actual map-tile file layout: filenames must
   be 8.3-format unless `CONFIG_FS_FATFS_LFN` is enabled (a name like
   `stravav11_test.txt` fails `fs_open()` with `-ENOENT`, not an
   obviously-named length error) — so either keep on-device tile
   filenames within 8.3 (e.g. hex-encoded tile coordinates), or enable
   LFN when the time comes.
2. **RAM is the tightest constraint**, and it's shared with ANT+/BLE/
   display/everything else already running concurrently — the tile cache
   size has to be sized empirically against real peak usage, not assumed.
3. **No existing parser for anything map-shaped** — the on-device
   tile-loading/parsing code is new, unlike most of this port which has
   been "carry the logic, replace the plumbing." (See the library survey
   below for whether any of this can be reused rather than written from
   scratch.)
4. **LS027 SPI throughput for panning smoothness** hasn't been profiled —
   full-frame `display_write()` calls complete in the existing demos, but
   redraw latency under a denser vector scene (hundreds of line segments)
   at pan/zoom-interaction speed (vs. GPS-1Hz-driven redraw) is
   unmeasured.
5. **No design work yet for how zoom/pan is actually triggered** — buttons
   exist on the real board (P0.14/P0.13/P0.11) but no input-handling code
   has been ported (`button.h`/`Notif.h` are explicitly still un-ported
   per Phase 7).

## Suggested phased plan

1. ~~Validate `CONFIG_FAT_FILESYSTEM_ELM` actually mounts on the SD
   card.~~ **Done** — see risk #1 above.
2. Build the offline OSM-to-binary-tile conversion tool (host-side
   Python, same lineage as `gpx_to_c.py`), test against a small real
   extract of the riding area.
3. On-device: file-based tile loader + a minimal parser, tested on
   `native_sim` first (matches this port's established "logic before
   hardware" discipline).
4. Wire the (currently dead) `Zoom` projection math to real rendering via
   `ZephyrGFX`, validate against `native_sim` with a fixed test tile.
5. Hardware bring-up on the real board: real SD-backed tiles, real
   GPS-driven panning, buttons for zoom (new input-handling work).

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
