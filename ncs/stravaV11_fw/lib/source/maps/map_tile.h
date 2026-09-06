/*
 * Reads tile_<lat_idx>_<lon_idx>.bin files produced by
 * tools/osm_to_tiles.py -- see that script's docstring for the full
 * format spec (docs/maps_feasibility.md has the summary). This is a
 * pure buffer decoder: no file I/O, no dynamic allocation, so it's
 * testable on native_sim without a filesystem or SD card attached.
 * Loading the file itself (fs_open/fs_read on the real SD card, already
 * validated working in stravaV11_fw's sd_fat_demo()) is separate,
 * hardware-side glue, not part of this module.
 *
 * Deliberately not a bounded-array-copy decoder: never materializes a
 * whole tile's points as a separate float array (docs/maps_feasibility.md
 * flags RAM as the tightest constraint for this feature) -- instead it's
 * a stateful iterator directly over the caller-owned buffer, converting
 * one point to float lat/lon at a time, on demand.
 */

#ifndef MAP_TILE_H_
#define MAP_TILE_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAP_TILE_MAGIC     "SV11MAP1"
#define MAP_TILE_NAME_MAX  32

/* Must match tools/osm_to_tiles.py's TILE_DEG exactly. */
#define MAP_TILE_DEG 0.02

enum map_tile_error {
	MAP_TILE_OK = 0,
	MAP_TILE_ERR_TOO_SHORT = -1, /* buffer shorter than the fixed header */
	MAP_TILE_ERR_BAD_MAGIC = -2,
	MAP_TILE_ERR_TRUNCATED = -3, /* a polyline's declared size runs past the buffer */
};

struct map_tile_iter {
	const uint8_t *buf;
	size_t len;
	size_t offset;
	int32_t origin_lat_e6;
	int32_t origin_lon_e6;
	uint16_t polylines_total;
	uint16_t polylines_seen;
};

struct map_tile_polyline {
	uint8_t road_class;
	uint16_t point_count;
	size_t points_offset; /* byte offset into the buffer map_tile_iter_init() was given */
};

/* Validates the header (magic + fixed-size fields fit in `len`) and
 * initializes `it` for iteration via map_tile_iter_next(). Returns
 * MAP_TILE_OK on success, otherwise a negative map_tile_error. */
int map_tile_iter_init(struct map_tile_iter *it, const uint8_t *buf, size_t len);

/* Advances to the next polyline. Returns 1 and fills *out if one was
 * available, 0 if iteration finished cleanly (all polylines consumed),
 * a negative map_tile_error if the buffer is truncated mid-polyline. */
int map_tile_iter_next(struct map_tile_iter *it, struct map_tile_polyline *out);

/* Decodes point `index` (0-based, < pl->point_count) of a polyline
 * returned by map_tile_iter_next() into absolute lat/lon degrees. `it`
 * must be the same iterator the polyline came from (for the tile
 * origin) -- does not re-validate bounds, callers must respect
 * pl->point_count. */
void map_tile_point_at(const struct map_tile_iter *it, const struct map_tile_polyline *pl,
			uint16_t index, float *lat, float *lon);

/* Computes which tile a lat/lon falls in and formats
 * "tile_<lat_idx>_<lon_idx>.bin" into name_out (>= MAP_TILE_NAME_MAX
 * bytes). Matches tools/osm_to_tiles.py's grid and naming exactly --
 * uses double precision internally for the same reason Python's default
 * float (also a double) does: avoids a float32-vs-float64 rounding
 * mismatch right at a tile boundary. */
void map_tile_name_for(float lat, float lon, char *name_out);

#ifdef __cplusplus
}
#endif

#endif /* MAP_TILE_H_ */
