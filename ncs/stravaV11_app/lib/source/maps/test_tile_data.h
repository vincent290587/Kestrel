/*
 * Real output of tools/osm_to_tiles.py against a small synthetic test
 * PBF (built with osmium.SimpleWriter, not downloaded -- see that
 * script's own validation notes in docs/maps_feasibility.md), not
 * hand-crafted bytes. This is tile_2_2.bin from that run: one
 * "primary"-class way with a real Douglas-Peucker-preserved deviation
 * point, tile origin (0.04, 0.04) chosen specifically because it's
 * non-zero -- exercises map_tile_point_at()'s origin+delta addition,
 * which a tile at origin (0,0) wouldn't.
 */

#ifndef TEST_TILE_DATA_H_
#define TEST_TILE_DATA_H_

#include <stdint.h>

static const uint8_t test_tile_bytes[] = {
	0x53, 0x56, 0x31, 0x31, 0x4d, 0x41, 0x50, 0x31, 0x40, 0x9c, 0x00, 0x00,
	0x40, 0x9c, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x05, 0x00, 0x00,
	0x10, 0x27, 0x10, 0x27, 0x10, 0x27, 0xb0, 0x36, 0xf8, 0x2a, 0x80, 0x3e,
	0x10, 0x27, 0x50, 0x46, 0x10, 0x27, 0x20, 0x4e,
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

#endif /* TEST_TILE_DATA_H_ */
