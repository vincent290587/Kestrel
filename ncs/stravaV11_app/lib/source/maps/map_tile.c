#include <math.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/sys/byteorder.h>

#include "map_tile.h"

/* Layout constants mirror tools/osm_to_tiles.py's struct.calcsize()
 * results exactly (HEADER_FMT "<8siiHxx", POLYLINE_HDR_FMT "<BHx",
 * POINT_FMT "<hh") -- kept as plain byte offsets/sizes here rather than
 * a packed C struct cast, matching this project's existing wire-format
 * parsing convention (bt_cp_client.c's sys_get_le16/32(), not a struct
 * overlay) for portability across alignment/padding rules. */
#define HEADER_SIZE        20 /* 8s magic + i32 lat0 + i32 lon0 + u16 count + 2 pad */
#define POLYLINE_HDR_SIZE  4  /* u8 road_class + u16 point_count + 1 pad */
#define POINT_SIZE         4  /* i16 dlat + i16 dlon */

int map_tile_iter_init(struct map_tile_iter *it, const uint8_t *buf, size_t len)
{
	if (len < HEADER_SIZE) {
		return MAP_TILE_ERR_TOO_SHORT;
	}
	if (memcmp(buf, MAP_TILE_MAGIC, 8) != 0) {
		return MAP_TILE_ERR_BAD_MAGIC;
	}

	it->buf = buf;
	it->len = len;
	it->offset = HEADER_SIZE;
	it->origin_lat_e6 = (int32_t)sys_get_le32(buf + 8);
	it->origin_lon_e6 = (int32_t)sys_get_le32(buf + 12);
	it->polylines_total = sys_get_le16(buf + 16);
	it->polylines_seen = 0;

	return MAP_TILE_OK;
}

int map_tile_iter_next(struct map_tile_iter *it, struct map_tile_polyline *out)
{
	if (it->polylines_seen >= it->polylines_total) {
		return 0;
	}
	if (it->offset + POLYLINE_HDR_SIZE > it->len) {
		return MAP_TILE_ERR_TRUNCATED;
	}

	uint8_t road_class = it->buf[it->offset];
	uint16_t point_count = sys_get_le16(it->buf + it->offset + 1);
	size_t points_offset = it->offset + POLYLINE_HDR_SIZE;
	size_t points_size = (size_t)point_count * POINT_SIZE;

	if (points_offset + points_size > it->len) {
		return MAP_TILE_ERR_TRUNCATED;
	}

	out->road_class = road_class;
	out->point_count = point_count;
	out->points_offset = points_offset;

	it->offset = points_offset + points_size;
	it->polylines_seen++;

	return 1;
}

void map_tile_point_at(const struct map_tile_iter *it, const struct map_tile_polyline *pl,
			uint16_t index, float *lat, float *lon)
{
	size_t off = pl->points_offset + (size_t)index * POINT_SIZE;
	int16_t dlat = (int16_t)sys_get_le16(it->buf + off);
	int16_t dlon = (int16_t)sys_get_le16(it->buf + off + 2);

	*lat = (float)((double)it->origin_lat_e6 / 1e6 + (double)dlat / 1e6);
	*lon = (float)((double)it->origin_lon_e6 / 1e6 + (double)dlon / 1e6);
}

void map_tile_name_for(float lat, float lon, char *name_out)
{
	int32_t lat_idx = (int32_t)floor((double)lat / MAP_TILE_DEG);
	int32_t lon_idx = (int32_t)floor((double)lon / MAP_TILE_DEG);

	snprintf(name_out, MAP_TILE_NAME_MAX, "tile_%d_%d.bin", (int)lat_idx, (int)lon_idx);
}
