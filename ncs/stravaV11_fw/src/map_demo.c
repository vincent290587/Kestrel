/*
 * Maps-feasibility step 3.5 (docs/maps_feasibility.md): wires the
 * hardware-agnostic map_tile.c parser (validated on native_sim in
 * stravaV11_app) to the real SD card via Zephyr's fs API -- the
 * fs_open()/fs_read() glue that native_sim validation deliberately left
 * undone, since it's hardware-dependent and was already de-risked by
 * sd_fat_demo()'s own real file round trip.
 *
 * Self-contained, same rigor as every other storage demo in this file:
 * writes a real tool-generated test tile (the same tile_2_2.bin bytes
 * stravaV11_app's smoke test already validated, embedded in
 * test_tile_data.h) to the SD card first, then loads and parses it back
 * using *only* the generic loading path a real GPS-driven lookup would
 * use -- map_tile_name_for() to compute the filename, fs_open() by that
 * name, fs_read(), then map_tile_iter parsing -- not any special
 * knowledge of the buffer this same demo just wrote. Producing real map
 * data for a real riding area (running tools/osm_to_tiles.py against an
 * actual OSM extract and copying the results onto the card by some other
 * means) is a separate, later step; this only proves the on-device
 * loading pipeline itself works.
 */

#include <math.h>
#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#if defined(CONFIG_FAT_FILESYSTEM_ELM)
#include <zephyr/fs/fs.h>
#include <ff.h>
#endif

#include "map_tile.h"
#include "test_tile_data.h"
#include "gfx_demo.h"
#include "map_demo.h"

#if defined(CONFIG_FAT_FILESYSTEM_ELM)

static void report_polyline(const struct map_tile_iter *it, const struct map_tile_polyline *pl,
			     int polyline_idx)
{
	printk("map_demo: polyline %d: road_class=%u points=%u\n", polyline_idx, pl->road_class,
	       pl->point_count);

	for (uint16_t i = 0; i < pl->point_count; i++) {
		float lat = 0.f, lon = 0.f, alt = 0.f;

		map_tile_point_at(it, pl, i, &lat, &lon, &alt);
		if ((int)alt == MAP_TILE_ALT_UNKNOWN_M) {
			printk("map_demo:   point %u: lat=%d.%06d lon=%d.%06d alt=unknown\n", i,
			       (int)lat, (int)(fabsf(lat - (int)lat) * 1000000), (int)lon,
			       (int)(fabsf(lon - (int)lon) * 1000000));
		} else {
			printk("map_demo:   point %u: lat=%d.%06d lon=%d.%06d alt=%dm\n", i, (int)lat,
			       (int)(fabsf(lat - (int)lat) * 1000000), (int)lon,
			       (int)(fabsf(lon - (int)lon) * 1000000), (int)alt);
		}
	}
}

void map_demo(void)
{
	static FATFS fat_fs;
	static struct fs_mount_t mp = {
		.type = FS_FATFS,
		.fs_data = &fat_fs,
		.mnt_point = "/SD:",
	};

	int err = fs_mount(&mp);

	printk("map_demo: fs_mount(\"/SD:\") -> %d\n", err);
	if (err != 0) {
		return;
	}

	/* This is the exact point stravaV11_app's smoke test verified
	 * resolves to "tile_2_2.bin" -- reused here so the filename this
	 * demo seeds is the same one a real GPS-driven lookup for that
	 * point would ask for. */
	char name[MAP_TILE_NAME_MAX];

	map_tile_name_for(0.05f, 0.05f, name);

	char path[48];

	snprintf(path, sizeof(path), "/SD:/%s", name);

	struct fs_file_t file;

	fs_file_t_init(&file);
	err = fs_open(&file, path, FS_O_CREATE | FS_O_WRITE);
	if (err == 0) {
		ssize_t written = fs_write(&file, test_tile_bytes, sizeof(test_tile_bytes));

		printk("map_demo: seeded %s (%zu bytes), fs_write() -> %d\n", path,
		       sizeof(test_tile_bytes), (int)written);
		fs_close(&file);
	} else {
		printk("map_demo: fs_open(write, %s) -> %d\n", path, err);
		fs_unmount(&mp);
		return;
	}

	/* Now the actual point of this demo: load the file back using only
	 * the generic loading path. */
	static uint8_t load_buf[512];

	fs_file_t_init(&file);
	err = fs_open(&file, path, FS_O_READ);
	printk("map_demo: fs_open(read, %s) -> %d\n", path, err);
	if (err != 0) {
		fs_unmount(&mp);
		return;
	}

	ssize_t bytes_read = fs_read(&file, load_buf, sizeof(load_buf));

	fs_close(&file);
	printk("map_demo: fs_read() -> %d bytes\n", (int)bytes_read);

	if (bytes_read <= 0) {
		fs_unmount(&mp);
		return;
	}

	struct map_tile_iter it;
	int rc = map_tile_iter_init(&it, load_buf, (size_t)bytes_read);

	printk("map_demo: map_tile_iter_init() -> %d\n", rc);

	if (rc == MAP_TILE_OK) {
		struct map_tile_polyline pl;
		int got;
		int polyline_idx = 0;

		while ((got = map_tile_iter_next(&it, &pl)) == 1) {
			report_polyline(&it, &pl, polyline_idx);
			polyline_idx++;
		}
		printk("map_demo: iteration ended -> %d (0 = clean), %d polyline(s)\n", got,
		       polyline_idx);

		/* Maps-feasibility phased plan step 4/5: actually draw the
		 * loaded tile, centered on the same point it was seeded at,
		 * at the fixed constant zoom (map_render.h's
		 * MAP_RENDER_ZOOM_LEVEL). */
		gfx_demo_show_map(load_buf, (size_t)bytes_read, 0.05f, 0.05f);
	}

	fs_unmount(&mp);
}

#else /* !CONFIG_FAT_FILESYSTEM_ELM */

void map_demo(void)
{
	printk("map_demo: skipped (CONFIG_FAT_FILESYSTEM_ELM not enabled)\n");
}

#endif
