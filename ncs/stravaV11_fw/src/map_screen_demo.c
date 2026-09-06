/*
 * Live GPS-driven map screen (docs/maps_feasibility.md, maps-feasibility
 * phased plan step 5) -- SD-card-free variant for isolating a real
 * problem found during hardware validation: after a few minutes of the
 * SD-card-backed version running (fs_mount/fs_write/fs_open/fs_read
 * every redraw cycle, concurrent with QSPI/ANT+/BLE/display), the board
 * lost power outright (POWER.RESETREAS read 0 immediately after the
 * reboot -- a genuine power-on reset, not a software crash), and no
 * render was ever observed on the physical screen even before that.
 * Bypassing the SD card and filesystem entirely -- rendering straight
 * from real_route_tiles.h, real OpenStreetMap data linked into flash --
 * isolates whether the render/display path itself works at all,
 * independent of any SD card current draw or FAT filesystem behavior.
 * If this still doesn't render or still loses power, the SD card is
 * cleared as a cause; if it works cleanly, the SD path is implicated.
 *
 * Swapped in for sensor_screen_demo_start() in main() -- see that call
 * site's comment for how to swap back.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "map_tile.h"
#include "real_route_tiles.h"
#include "gps_demo.h"
#include "gfx_demo.h"
#include "map_screen_demo.h"

#define MAP_SCREEN_REFRESH_MS 1000

static void map_screen_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(map_screen_work, map_screen_work_handler);

static const struct embedded_tile *find_tile(const char *name)
{
	for (size_t i = 0; i < EMBEDDED_TILE_COUNT; i++) {
		if (strcmp(embedded_tiles[i].name, name) == 0) {
			return &embedded_tiles[i];
		}
	}
	return NULL;
}

static void map_screen_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	float lat = 0.f, lon = 0.f;

	if (!gps_demo_get_position(&lat, &lon)) {
		printk("map_screen: no GPS fix yet\n");
		k_work_schedule(&map_screen_work, K_MSEC(MAP_SCREEN_REFRESH_MS));
		return;
	}

	char name[MAP_TILE_NAME_MAX];

	map_tile_name_for(lat, lon, name);

	const struct embedded_tile *tile = find_tile(name);

	if (tile == NULL) {
		printk("map_screen: %s not embedded (out of range)\n", name);
		k_work_schedule(&map_screen_work, K_MSEC(MAP_SCREEN_REFRESH_MS));
		return;
	}

	printk("map_screen: rendering %s (%zu bytes) centered at lat=%d lon=%d (x1000)\n", name,
	       tile->len, (int)(lat * 1000), (int)(lon * 1000));

	gfx_demo_show_map(tile->data, tile->len, lat, lon);

	k_work_schedule(&map_screen_work, K_MSEC(MAP_SCREEN_REFRESH_MS));
}

void map_screen_demo_start(void)
{
	printk("map_screen: starting, %d embedded tile(s), no SD card involved\n",
	       EMBEDDED_TILE_COUNT);
	k_work_schedule(&map_screen_work, K_NO_WAIT);
}
