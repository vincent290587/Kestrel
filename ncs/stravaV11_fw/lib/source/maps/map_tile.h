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

#define MAP_TILE_MAGIC     "SV11MAP2"
#define MAP_TILE_NAME_MAX  32

/* Must match tools/osm_to_tiles.py's TILE_DEG exactly. */
#define MAP_TILE_DEG 0.02

/* Must match tools/osm_to_tiles.py's ALT_UNKNOWN_M exactly: no ele tag
 * was available for this point when the tile was generated (true for
 * most ordinary road/path points -- see that script's docstring). */
#define MAP_TILE_ALT_UNKNOWN_M (-32768)

/* Must match tools/osm_to_tiles.py's ROAD_CLASSES exactly. Only cycleway
 * gets a named constant -- it's the one class map_render.cpp treats
 * specially (kept and drawn with extra visual weight, since this is a
 * cycling computer), everything else is referred to by its plain numeric
 * class (0-3 motorway/trunk .. tertiary/residential, most to least major;
 * 4 service/track; 6 foot-only path/footway/bridleway). */
#define MAP_TILE_ROAD_CLASS_CYCLEWAY 5

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
 * returned by map_tile_iter_next() into absolute lat/lon degrees plus
 * altitude in metres (*alt set to MAP_TILE_ALT_UNKNOWN_M, not NAN, when
 * no ele tag was available for this point -- callers must check for
 * that sentinel explicitly). `it` must be the same iterator the
 * polyline came from (for the tile origin) -- does not re-validate
 * bounds, callers must respect pl->point_count. */
void map_tile_point_at(const struct map_tile_iter *it, const struct map_tile_polyline *pl,
			uint16_t index, float *lat, float *lon, float *alt);

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
