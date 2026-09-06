/*
 * Real output of tools/osm_to_tiles.py against a small synthetic test
 * PBF (built with osmium.SimpleWriter, not downloaded -- see that
 * script's own validation notes in docs/maps_feasibility.md), not
 * hand-crafted bytes. This is tile_2_2.bin from that run (format v2,
 * magic "SV11MAP2"): one "primary"-class way with a real
 * Douglas-Peucker-preserved deviation point, tile origin (0.04, 0.04)
 * chosen specifically because it's non-zero -- exercises
 * map_tile_point_at()'s origin+delta addition, which a tile at origin
 * (0,0) wouldn't. Altitude deliberately covers all three real cases:
 * point 0 has a real numeric ele tag (45), point 2 has a real numeric
 * ele tag given as a fractional string ("123.5", rounds to 124), and
 * points 1/3/4 have no ele tag (or a garbage non-numeric one) and so
 * decode to MAP_TILE_ALT_UNKNOWN_M -- see tools/make_test_pbf.py for the
 * exact source tags and how to regenerate this file if the format or
 * that script's test data changes again.
 */

#ifndef TEST_TILE_DATA_H_
#define TEST_TILE_DATA_H_

#include <stdint.h>

#include "map_tile.h"

static const uint8_t test_tile_bytes[] = {
	0x53, 0x56, 0x31, 0x31, 0x4d, 0x41, 0x50, 0x32, 0x40, 0x9c, 0x00, 0x00,
	0x40, 0x9c, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x05, 0x00, 0x00,
	0x10, 0x27, 0x10, 0x27, 0x2d, 0x00, 0x10, 0x27, 0xb0, 0x36, 0x00, 0x80,
	0xf8, 0x2a, 0x80, 0x3e, 0x7c, 0x00, 0x10, 0x27, 0x50, 0x46, 0x00, 0x80,
	0x10, 0x27, 0x20, 0x4e, 0x00, 0x80,
};

/* Expected decode, for the smoke test to check against -- computed by
 * hand-decoding the same bytes with Python's struct module, not just
 * trusting the C parser to agree with itself. */
#define TEST_TILE_EXPECTED_ROAD_CLASS 1
#define TEST_TILE_EXPECTED_POINT_COUNT 5
static const float test_tile_expected_lat[TEST_TILE_EXPECTED_POINT_COUNT] = {
	0.050000f, 0.050000f, 0.051000f, 0.050000f, 0.050000f,
};
static const float test_tile_expected_lon[TEST_TILE_EXPECTED_POINT_COUNT] = {
	0.050000f, 0.054000f, 0.056000f, 0.058000f, 0.060000f,
};
static const float test_tile_expected_alt[TEST_TILE_EXPECTED_POINT_COUNT] = {
	45.0f,
	(float)MAP_TILE_ALT_UNKNOWN_M,
	124.0f,
	(float)MAP_TILE_ALT_UNKNOWN_M,
	(float)MAP_TILE_ALT_UNKNOWN_M,
};

#endif /* TEST_TILE_DATA_H_ */
