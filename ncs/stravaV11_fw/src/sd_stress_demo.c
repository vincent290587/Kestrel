/*
 * Heavy, sustained SD-card I/O stress test (docs/maps_feasibility.md,
 * risk #6): a genuine power-on reset (POWER.RESETREAS read 0 -- not a
 * software crash) was found a few minutes into SD-card-backed live map
 * redraws running concurrently with the full subsystem set. That
 * investigation isolated a separate, unrelated render bug (fixed, see
 * gps_demo.cpp) by stripping main() down to almost nothing, which also
 * happened to remove all SD-card I/O -- so it never actually re-tested
 * whether SD-card I/O specifically (as opposed to the full concurrent
 * load in general) still loses power now that main() is restored. This
 * runs a deliberately heavier, faster cycle than the original map
 * loading (32KB every 500ms via fs_write()/fs_read(), vs ~10KB every
 * 1000ms) so if SD I/O under concurrent load is still a problem, it
 * should surface faster and more reliably here than waiting for the
 * real map screen to happen to need a reload.
 *
 * Mounts its own "/SD:" volume (separate FATFS/fs_mount_t instance from
 * sd_fat_demo()'s in main.c) and keeps it mounted for the process
 * lifetime, matching the original SD-backed map_screen_demo's approach.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/time_units.h>

#if defined(CONFIG_FAT_FILESYSTEM_ELM)
#include <zephyr/fs/fs.h>
#include <ff.h>
#endif

#include "sd_stress_demo.h"

#if defined(CONFIG_FAT_FILESYSTEM_ELM)

#define SD_STRESS_INTERVAL_MS 500
#define SD_STRESS_BUF_SIZE    (32 * 1024)
#define SD_STRESS_PATH        "/SD:/stress.bin"

static FATFS s_fat_fs;
static struct fs_mount_t s_mp = {
	.type = FS_FATFS,
	.fs_data = &s_fat_fs,
	.mnt_point = "/SD:",
};
static uint8_t s_write_buf[SD_STRESS_BUF_SIZE];
static uint8_t s_read_buf[SD_STRESS_BUF_SIZE];
static uint32_t s_cycle;

static void sd_stress_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(sd_stress_work, sd_stress_work_handler);

static void sd_stress_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	for (size_t i = 0; i < SD_STRESS_BUF_SIZE; i++) {
		s_write_buf[i] = (uint8_t)(i + s_cycle);
	}

	struct fs_file_t file;

	fs_file_t_init(&file);
	int err = fs_open(&file, SD_STRESS_PATH, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);

	if (err != 0) {
		printk("sd_stress: fs_open(write) -> %d\n", err);
		k_work_schedule(&sd_stress_work, K_MSEC(SD_STRESS_INTERVAL_MS));
		return;
	}

	uint32_t write_start = k_cycle_get_32();
	ssize_t written = fs_write(&file, s_write_buf, sizeof(s_write_buf));
	uint32_t write_us = k_cyc_to_us_floor32(k_cycle_get_32() - write_start);

	fs_close(&file);

	fs_file_t_init(&file);
	err = fs_open(&file, SD_STRESS_PATH, FS_O_READ);

	if (err != 0) {
		printk("sd_stress: cycle %u write=%d bytes in %u us, fs_open(read) -> %d\n",
		       s_cycle, (int)written, write_us, err);
		k_work_schedule(&sd_stress_work, K_MSEC(SD_STRESS_INTERVAL_MS));
		s_cycle++;
		return;
	}

	uint32_t read_start = k_cycle_get_32();
	ssize_t bytes_read = fs_read(&file, s_read_buf, sizeof(s_read_buf));
	uint32_t read_us = k_cyc_to_us_floor32(k_cycle_get_32() - read_start);

	fs_close(&file);

	bool match = (bytes_read == (ssize_t)sizeof(s_read_buf)) &&
		     (memcmp(s_write_buf, s_read_buf, sizeof(s_write_buf)) == 0);

	printk("sd_stress: cycle %u write=%d bytes in %u us, read=%d bytes in %u us, %s\n", s_cycle,
	       (int)written, write_us, (int)bytes_read, read_us, match ? "MATCH" : "MISMATCH");

	s_cycle++;
	k_work_schedule(&sd_stress_work, K_MSEC(SD_STRESS_INTERVAL_MS));
}

void sd_stress_demo_start(void)
{
	int err = fs_mount(&s_mp);

	printk("sd_stress: fs_mount(\"/SD:\") -> %d, %uKB every %dms\n", err,
	       SD_STRESS_BUF_SIZE / 1024, SD_STRESS_INTERVAL_MS);
	if (err != 0) {
		return;
	}

	k_work_schedule(&sd_stress_work, K_NO_WAIT);
}

#else /* !CONFIG_FAT_FILESYSTEM_ELM */

void sd_stress_demo_start(void)
{
	printk("sd_stress: skipped (CONFIG_FAT_FILESYSTEM_ELM not enabled)\n");
}

#endif
