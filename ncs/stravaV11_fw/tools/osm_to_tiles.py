#!/usr/bin/env python3
"""
Converts an OpenStreetMap PBF extract into stravaV11's on-device map-tile
binary format, for the maps feature outlined in docs/maps_feasibility.md
(step 2 of that doc's phased plan). Meant to run on a small extract
already clipped to the riding area (e.g. via `osmium extract`), not a
whole-country file -- this tool does its own bbox filtering too, but that
only skips ways outside the box, it doesn't speed up parsing a huge input.

Dependency: the `osmium` package (PyPI name is "osmium", not "pyosmium" --
that's just the upstream project's name; `pip install osmium` in this
repo's .venv). Not stdlib-only, unlike gpx_to_c.py -- real PBF parsing
needs a real library, and osmium is the standard one (confirmed via the
library survey in docs/maps_feasibility.md).

------------------------------------------------------------------------
On-device tile format (little-endian, matches the nRF52840's byte order
-- no swapping needed when the firmware casts a loaded buffer to a
struct):

  Header (16 bytes):
    magic          8s   b"SV11MAP1"
    tile_lat0_e6   i32  tile's SW-corner latitude,  1e-6 degree units
    tile_lon0_e6   i32  tile's SW-corner longitude, 1e-6 degree units
    polyline_count u16
    (2 bytes padding to keep polylines 4-byte aligned)

  Then polyline_count polylines, each:
    road_class     u8   see ROAD_CLASSES below
    point_count    u16
    (1 byte padding)
    points         point_count * (dlat: i16, dlon: i16), 1e-6 degree
                   units relative to (tile_lat0_e6, tile_lon0_e6)

Points are quantized to 1e-6 degree (about 11cm at the equator -- an
order of magnitude finer than the simplification tolerance below, so
quantization itself isn't a meaningful source of error). A tile spans
TILE_DEG degrees, so TILE_DEG*1e6 must stay under 32767 (int16 range) --
enforced by an assertion, not just documentation.

Ways are simplified (Douglas-Peucker) once in full, *then* split into
per-tile polylines; a point that falls exactly on a tile boundary is
duplicated into both tiles' polylines so lines drawn tile-by-tile still
connect visually at the seam.

Filenames are not 8.3-constrained (CONFIG_FS_FATFS_LFN is enabled, see
CLAUDE.md) -- tiles are named "tile_<lat_idx>_<lon_idx>.bin", one flat
directory, no sub-folders (fine for a single-region extract; a
whole-country conversion would want the OSM_Extract-style folder
grouping documented in docs/maps_feasibility.md, not implemented here).
------------------------------------------------------------------------

Usage:
    python3 osm_to_tiles.py <input.osm.pbf> <output_dir> \
        [--bbox min_lon min_lat max_lon max_lat] [--epsilon degrees]

    python3 osm_to_tiles.py --dump <tile.bin>
        Prints a tile file's contents (header + polylines + point count)
        for inspection/verification -- no PBF/osmium dependency needed
        for this mode.
"""
import argparse
import math
import os
import struct
import sys

import osmium

MAGIC = b"SV11MAP1"
TILE_DEG = 0.02  # ~2.2km latitude span; ~1.1-2.2km longitude depending on latitude
assert TILE_DEG * 1e6 < 32767, "TILE_DEG too large for int16 quantized points"

DEFAULT_EPSILON_DEG = 0.00015  # ~16.7m at the equator (Douglas-Peucker tolerance)

# highway=* value -> road class byte. Ways with no matching tag are dropped.
# Classes are ordered by importance (0 = most major) so a future renderer
# can decimate by class at low zoom without needing a second field.
ROAD_CLASSES = {
    "motorway": 0, "motorway_link": 0,
    "trunk": 0, "trunk_link": 0,
    "primary": 1, "primary_link": 1,
    "secondary": 2, "secondary_link": 2,
    "tertiary": 3, "tertiary_link": 3,
    "unclassified": 3, "residential": 3,
    "service": 4, "track": 4,
    "cycleway": 5, "path": 5, "footway": 5, "bridleway": 5,
}

HEADER_FMT = "<8siiHxx"
HEADER_SIZE = struct.calcsize(HEADER_FMT)
POLYLINE_HDR_FMT = "<BHx"
POLYLINE_HDR_SIZE = struct.calcsize(POLYLINE_HDR_FMT)
POINT_FMT = "<hh"
POINT_SIZE = struct.calcsize(POINT_FMT)


def perp_distance(pt, start, end):
    if start == end:
        return math.hypot(pt[0] - start[0], pt[1] - start[1])
    x, y = pt
    x1, y1 = start
    x2, y2 = end
    dx, dy = x2 - x1, y2 - y1
    t = ((x - x1) * dx + (y - y1) * dy) / (dx * dx + dy * dy)
    t = max(0.0, min(1.0, t))
    px, py = x1 + t * dx, y1 + t * dy
    return math.hypot(x - px, y - py)


def douglas_peucker(points, epsilon):
    if len(points) < 3:
        return points
    dmax = 0.0
    index = 0
    for i in range(1, len(points) - 1):
        d = perp_distance(points[i], points[0], points[-1])
        if d > dmax:
            index = i
            dmax = d
    if dmax > epsilon:
        left = douglas_peucker(points[: index + 1], epsilon)
        right = douglas_peucker(points[index:], epsilon)
        return left[:-1] + right
    return [points[0], points[-1]]


def tile_index(lon, lat):
    return (math.floor(lat / TILE_DEG), math.floor(lon / TILE_DEG))


def split_into_tiles(points):
    """points: list of (lon, lat). Returns {(lat_idx, lon_idx): [segment, ...]}
    where each segment is a list of (lon, lat) points -- a single way can
    cross out of and back into the same tile, producing more than one
    segment for that tile."""
    tiles = {}
    if not points:
        return tiles

    current_tile = tile_index(*points[0])
    current_seg = [points[0]]

    for prev, pt in zip(points, points[1:]):
        pt_tile = tile_index(*pt)
        if pt_tile != current_tile:
            # Boundary crossing: close the outgoing segment with this point
            # (duplicated), then start the next segment from it too, so
            # both tiles' polylines include the crossing point.
            current_seg.append(pt)
            tiles.setdefault(current_tile, []).append(current_seg)
            current_tile = pt_tile
            current_seg = [pt]
        else:
            current_seg.append(pt)

    if len(current_seg) >= 2:
        tiles.setdefault(current_tile, []).append(current_seg)

    return tiles


def quantize(lat_idx, lon_idx, points):
    tile_lat0 = lat_idx * TILE_DEG
    tile_lon0 = lon_idx * TILE_DEG
    out = []
    for lon, lat in points:
        dlat = round((lat - tile_lat0) * 1e6)
        dlon = round((lon - tile_lon0) * 1e6)
        dlat = max(-32768, min(32767, dlat))
        dlon = max(-32768, min(32767, dlon))
        out.append((dlat, dlon))
    return out


class WayCollector(osmium.SimpleHandler):
    def __init__(self, bbox, epsilon):
        super().__init__()
        self.bbox = bbox
        self.epsilon = epsilon
        self.ways_seen = 0
        self.ways_kept = 0
        # {(lat_idx, lon_idx): [(road_class, [(dlat, dlon), ...]), ...]}
        self.tiles = {}

    def way(self, w):
        self.ways_seen += 1
        road_class = ROAD_CLASSES.get(w.tags.get("highway"))
        if road_class is None:
            return

        points = []
        for n in w.nodes:
            if not n.location.valid():
                return
            lon, lat = n.location.lon, n.location.lat
            if self.bbox and not (
                self.bbox[0] <= lon <= self.bbox[2] and self.bbox[1] <= lat <= self.bbox[3]
            ):
                continue
            points.append((lon, lat))

        if len(points) < 2:
            return

        simplified = douglas_peucker(points, self.epsilon)
        if len(simplified) < 2:
            return

        self.ways_kept += 1
        for (lat_idx, lon_idx), segments in split_into_tiles(simplified).items():
            for seg_points in segments:
                quantized = quantize(lat_idx, lon_idx, seg_points)
                self.tiles.setdefault((lat_idx, lon_idx), []).append((road_class, quantized))


def write_tile(out_dir, lat_idx, lon_idx, polylines):
    tile_lat0_e6 = round(lat_idx * TILE_DEG * 1e6)
    tile_lon0_e6 = round(lon_idx * TILE_DEG * 1e6)

    path = os.path.join(out_dir, f"tile_{lat_idx}_{lon_idx}.bin")
    with open(path, "wb") as f:
        f.write(struct.pack(HEADER_FMT, MAGIC, tile_lat0_e6, tile_lon0_e6, len(polylines)))
        for road_class, points in polylines:
            f.write(struct.pack(POLYLINE_HDR_FMT, road_class, len(points)))
            for dlat, dlon in points:
                f.write(struct.pack(POINT_FMT, dlat, dlon))
    return path, os.path.getsize(path)


def dump_tile(path):
    with open(path, "rb") as f:
        data = f.read()

    magic, tile_lat0_e6, tile_lon0_e6, polyline_count = struct.unpack_from(HEADER_FMT, data, 0)
    if magic != MAGIC:
        print(f"BAD MAGIC: {magic!r} (expected {MAGIC!r})")
        return

    print(f"{path}: {len(data)} bytes")
    print(f"  tile origin (SW corner): lat={tile_lat0_e6 / 1e6:.6f} lon={tile_lon0_e6 / 1e6:.6f}")
    print(f"  polylines: {polyline_count}")

    offset = HEADER_SIZE
    for i in range(polyline_count):
        road_class, point_count = struct.unpack_from(POLYLINE_HDR_FMT, data, offset)
        offset += POLYLINE_HDR_SIZE
        pts = []
        for _ in range(point_count):
            dlat, dlon = struct.unpack_from(POINT_FMT, data, offset)
            offset += POINT_SIZE
            pts.append(
                (tile_lat0_e6 / 1e6 + dlat / 1e6, tile_lon0_e6 / 1e6 + dlon / 1e6)
            )
        print(f"  [{i}] road_class={road_class} points={point_count} first={pts[0]} last={pts[-1]}")

    assert offset == len(data), f"trailing/missing bytes: parsed {offset}, file is {len(data)}"
    print("  OK: parsed exactly to end of file")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("input", nargs="?", help="input .osm.pbf file")
    parser.add_argument("output_dir", nargs="?", help="directory to write tile_<lat>_<lon>.bin files into")
    parser.add_argument("--bbox", nargs=4, type=float, metavar=("MIN_LON", "MIN_LAT", "MAX_LON", "MAX_LAT"))
    parser.add_argument("--epsilon", type=float, default=DEFAULT_EPSILON_DEG,
                         help=f"Douglas-Peucker simplification tolerance in degrees (default {DEFAULT_EPSILON_DEG})")
    parser.add_argument("--dump", metavar="TILE_BIN", help="dump one tile file's contents and exit")
    args = parser.parse_args()

    if args.dump:
        dump_tile(args.dump)
        return

    if not args.input or not args.output_dir:
        parser.error("input and output_dir are required unless --dump is given")

    os.makedirs(args.output_dir, exist_ok=True)

    collector = WayCollector(bbox=args.bbox, epsilon=args.epsilon)
    collector.apply_file(args.input, locations=True)

    total_bytes = 0
    total_polylines = 0
    for (lat_idx, lon_idx), polylines in sorted(collector.tiles.items()):
        _, size = write_tile(args.output_dir, lat_idx, lon_idx, polylines)
        total_bytes += size
        total_polylines += len(polylines)

    print(f"Ways seen: {collector.ways_seen}, kept (had a mapped highway tag): {collector.ways_kept}")
    print(f"Tiles written: {len(collector.tiles)} to {args.output_dir}")
    print(f"Total polylines: {total_polylines}, total tile bytes: {total_bytes}")


if __name__ == "__main__":
    main()
