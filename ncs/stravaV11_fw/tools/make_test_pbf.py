#!/usr/bin/env python3
"""
Generates a tiny synthetic .osm.pbf to test osm_to_tiles.py against,
covering: a way crossing a tile boundary (splitting), a way with a large
deviation the Douglas-Peucker simplifier should keep alongside several
near-collinear points it should drop, a way with an unmapped highway tag
(should be dropped), a way with no highway tag at all (should be
dropped), and (way B specifically) a realistic mix of nodes with a real
numeric ele tag, no ele tag at all, and a garbage non-numeric ele tag --
exercising ALT_UNKNOWN_M for the latter two cases.

No network access needed -- this is how osm_to_tiles.py's pipeline logic
itself was validated (see docs/maps_feasibility.md), separately from
actually having real map data for a real riding area.

Way B (the "primary" road with the deviation point) is the one whose
output (tile_2_2.bin) got embedded as stravaV11_app/lib/source/maps and
stravaV11_fw/lib/source/maps's test_tile_data.h -- if you change this
script's way B, regenerate that header too:

    python3 tools/make_test_pbf.py /tmp/test.osm.pbf
    python3 tools/osm_to_tiles.py /tmp/test.osm.pbf /tmp/tiles_out
    python3 tools/osm_to_tiles.py --dump /tmp/tiles_out/tile_2_2.bin

Usage: python3 make_test_pbf.py <output.osm.pbf>
"""
import sys

import osmium
import osmium.osm.mutable as mut


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "test.osm.pbf"
    writer = osmium.SimpleWriter(out_path)

    nid_counter = [1]

    def node(lon, lat, ele=None):
        tags = {"ele": ele} if ele is not None else {}
        writer.add_node(mut.Node(id=nid_counter[0], location=(lon, lat), version=1, tags=tags))
        n = nid_counter[0]
        nid_counter[0] += 1
        return n

    # Way A: crosses the tile boundary at lon=0.02 (TILE_DEG), residential.
    a1 = node(0.0100, 0.0100)
    a2 = node(0.0150, 0.0110)
    a3 = node(0.0250, 0.0120)
    a4 = node(0.0300, 0.0130)
    writer.add_way(mut.Way(id=1, nodes=[a1, a2, a3, a4], version=1,
                            tags={"highway": "residential"}))

    # Way B: near-collinear points along lat=0.05 except one point that
    # deviates by ~0.001 deg (~111m, well above the default ~16.7m
    # epsilon) -- DP should collapse the collinear run but keep the
    # deviating point. ele tags: b1 real numeric, b4 real numeric
    # (string form, as OSM tags always are), b6 garbage/non-numeric
    # (-> ALT_UNKNOWN_M), b2/b3/b5 no ele tag at all (-> ALT_UNKNOWN_M)
    # -- this way is what ends up embedded as the on-device test
    # fixture, so it deliberately covers all three altitude cases in
    # one polyline.
    b1 = node(0.0500, 0.0500, ele="45")
    b2 = node(0.0520, 0.0500001)
    b3 = node(0.0540, 0.0499999)
    b4 = node(0.0560, 0.0510000, ele="123.5")  # the real deviation
    b5 = node(0.0580, 0.0500001)
    b6 = node(0.0600, 0.0500000, ele="unknown")
    writer.add_way(mut.Way(id=2, nodes=[b1, b2, b3, b4, b5, b6], version=1,
                            tags={"highway": "primary"}))

    # Way C: unmapped highway tag -- should be dropped entirely.
    c1 = node(0.0700, 0.0700)
    c2 = node(0.0710, 0.0710)
    writer.add_way(mut.Way(id=3, nodes=[c1, c2], version=1, tags={"highway": "steps"}))

    # Way D: no highway tag at all -- should be dropped entirely.
    d1 = node(0.0800, 0.0800)
    d2 = node(0.0810, 0.0810)
    writer.add_way(mut.Way(id=4, nodes=[d1, d2], version=1, tags={"building": "yes"}))

    # Way E: genuinely collinear points along a straight line -- DP
    # should collapse this to just the two endpoints, unambiguously
    # demonstrating simplification actually reduces point count (way
    # B's recursive split around its one real deviation point
    # legitimately keeps some of its other points too -- that's correct
    # DP behavior, not something this way is meant to also demonstrate).
    e1 = node(0.0900, 0.0900)
    e2 = node(0.0920, 0.0900)
    e3 = node(0.0940, 0.0900)
    e4 = node(0.0960, 0.0900)
    e5 = node(0.0980, 0.0900)
    e6 = node(0.1000, 0.0900)
    writer.add_way(mut.Way(id=5, nodes=[e1, e2, e3, e4, e5, e6], version=1,
                            tags={"highway": "tertiary"}))

    writer.close()
    print(f"wrote {out_path}")


if __name__ == "__main__":
    main()
