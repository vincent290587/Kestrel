/*
 * Live GPS-driven map screen (docs/maps_feasibility.md, maps-feasibility
 * phased plan step 5): seeds the real Rotterdam tiles
 * (real_route_tiles.h, real OpenStreetMap data -- still the only way to
 * get files onto this SD card, since USB MSC isn't wired up, Phase 9)
 * onto the SD card once at start, then periodically (~1Hz, matching
 * gps_sim_demo.c's replay rate) re-centers and redraws the map on the
 * current GPS fix, loading whichever tile the position currently falls
 * into via fs_open()/fs_read() -- a real SD-card round trip every
 * cycle, not the direct-from-flash shortcut used to isolate the render
 * bug -- and switching tiles correctly if/when it crosses a boundary.
 *
 * Two real problems blocked this working before, both now resolved:
 * (1) Locator::getPosition()'s single-consumer read meant
 * gps_demo_get_position() never once saw a fix (fixed in gps_demo.cpp,
 * unrelated to SD at all); (2) a genuine power-on reset under sustained
 * SD I/O concurrent with the full subsystem set (mitigated with a
 * self-healing STC3100 power latch, then re-tested clean over 6.7
 * minutes with sd_stress_demo.c's deliberately heavier load). With both
 * resolved, this reintroduces the real SD-card round trip this feature
 * always needed for real-world map coverage beyond what fits in flash.
 *
 * Mounts and unmounts its own "/SD:" volume (a separate FATFS/
 * fs_mount_t instance from sd_fat_demo()'s and sd_stress_demo.c's own)
 * for each operation -- once to seed, then again every redraw cycle --
 * rather than holding a persistent mount. Found the hard way that
 * Zephyr's fs layer only allows one mount per path at a time
 * (`fs_mount()` returns -EBUSY otherwise): the first version of this
 * held "/SD:" mounted permanently, the same design sd_stress_demo.c
 * independently also used, and whichever of the two ran second could
 * never mount at all -- silently, since that single failing printk got
 * lost in a congested boot-time logging burst, making it look like this
 * module had simply stopped doing anything at all. Transient mount/
 * unmount per operation is the same already-proven-safe pattern
 * sd_fat_demo() and the original one-shot map_demo() both use.
 */

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/time_units.h>

#if defined(CONFIG_FAT_FILESYSTEM_ELM)
#include <zephyr/fs/fs.h>
#include <ff.h>
#endif

#include "map_tile.h"
#include "real_route_tiles.h"
#include "gps_demo.h"
#include "gfx_demo.h"
#include "map_screen_demo.h"

#if defined(CONFIG_FAT_FILESYSTEM_ELM)

#define MAP_SCREEN_REFRESH_MS  1000
#define MAP_TILE_LOAD_BUF_SIZE 16384

static FATFS s_fat_fs;
static struct fs_mount_t s_mp = {
	.type = FS_FATFS,
	.fs_data = &s_fat_fs,
	.mnt_point = "/SD:",
};
static uint8_t s_load_buf[MAP_TILE_LOAD_BUF_SIZE];
static bool s_active;

static void map_screen_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(map_screen_work, map_screen_work_handler);

static bool seed_tiles(void)
{
	int err = fs_mount(&s_mp);

	printk("map_screen: fs_mount(\"/SD:\") -> %d\n", err);
	if (err != 0) {
		return false;
	}

	for (size_t i = 0; i < EMBEDDED_TILE_COUNT; i++) {
		char path[48];

		snprintf(path, sizeof(path), "/SD:/%s", embedded_tiles[i].name);

		struct fs_file_t file;

		fs_file_t_init(&file);
		int err = fs_open(&file, path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);

		if (err != 0) {
			printk("map_screen: fs_open(write, %s) -> %d\n", path, err);
			fs_unmount(&s_mp);
			return false;
		}

		ssize_t written = fs_write(&file, embedded_tiles[i].data, embedded_tiles[i].len);

		fs_close(&file);
		printk("map_screen: seeded %s (%zu bytes), fs_write() -> %d\n", path,
		       embedded_tiles[i].len, (int)written);

		if (written < 0 || (size_t)written != embedded_tiles[i].len) {
			fs_unmount(&s_mp);
			return false;
		}
	}

	fs_unmount(&s_mp);
	return true;
}

static void map_screen_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!s_active) {
		return;
	}

	float lat = 0.f, lon = 0.f;

	if (!gps_demo_get_position(&lat, &lon)) {
		k_work_schedule(&map_screen_work, K_MSEC(MAP_SCREEN_REFRESH_MS));
		return;
	}

	char name[MAP_TILE_NAME_MAX];

	map_tile_name_for(lat, lon, name);

	int err = fs_mount(&s_mp);

	if (err != 0) {
		printk("map_screen: fs_mount(\"/SD:\") -> %d\n", err);
		k_work_schedule(&map_screen_work, K_MSEC(MAP_SCREEN_REFRESH_MS));
		return;
	}

	char path[48];

	snprintf(path, sizeof(path), "/SD:/%s", name);

	struct fs_file_t file;

	fs_file_t_init(&file);
	err = fs_open(&file, path, FS_O_READ);

	if (err != 0) {
		printk("map_screen: fs_open(read, %s) -> %d (not seeded / out of range)\n", path,
		       err);
		fs_unmount(&s_mp);
		k_work_schedule(&map_screen_work, K_MSEC(MAP_SCREEN_REFRESH_MS));
		return;
	}

	uint32_t load_start = k_cycle_get_32();
	ssize_t bytes_read = fs_read(&file, s_load_buf, sizeof(s_load_buf));
	uint32_t load_us = k_cyc_to_us_floor32(k_cycle_get_32() - load_start);

	fs_close(&file);
	fs_unmount(&s_mp);

	if (bytes_read <= 0) {
		printk("map_screen: fs_read(%s) -> %d\n", path, (int)bytes_read);
		k_work_schedule(&map_screen_work, K_MSEC(MAP_SCREEN_REFRESH_MS));
		return;
	}

	printk("map_screen: %s, fs_read() -> %d bytes in %u us\n", path, (int)bytes_read, load_us);

	gfx_demo_show_map(s_load_buf, (size_t)bytes_read, lat, lon);

	k_work_schedule(&map_screen_work, K_MSEC(MAP_SCREEN_REFRESH_MS));
}

void map_screen_demo_start(void)
{
	if (s_active) {
		printk("map_screen: already running\n");
		return;
	}

	if (!seed_tiles()) {
		printk("map_screen: seeding failed, not starting redraw loop\n");
		return;
	}

	s_active = true;
	k_work_schedule(&map_screen_work, K_NO_WAIT);
}

void map_screen_demo_stop(void)
{
	if (!s_active) {
		printk("map_screen: not running\n");
		return;
	}
	s_active = false;
	printk("map_screen: stopping\n");
}

#else /* !CONFIG_FAT_FILESYSTEM_ELM */

void map_screen_demo_start(void)
{
	printk("map_screen: skipped (CONFIG_FAT_FILESYSTEM_ELM not enabled)\n");
}

void map_screen_demo_stop(void)
{
}

#endif
