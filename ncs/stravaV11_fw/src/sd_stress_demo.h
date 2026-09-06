#ifndef SD_STRESS_DEMO_H_
#define SD_STRESS_DEMO_H_

/* Heavy, sustained SD-card I/O stress test (docs/maps_feasibility.md,
 * risk #6): deliberately tests the actual configuration that originally
 * lost power -- the full subsystem set running concurrently with real
 * SD-card FAT file I/O -- rather than inferring from the stripped-down
 * configuration used to isolate the (separate, now-fixed) render bug.
 * Mounts its own "/SD:" volume and, every 500ms (faster than the
 * original map-loading cadence that first showed the power loss),
 * writes then reads back a 32KB buffer (larger than any real map tile
 * used so far) via fs_write()/fs_read(). No-ops cleanly if
 * CONFIG_FAT_FILESYSTEM_ELM isn't set. Call after storage_demo(). */
void sd_stress_demo_start(void);

#endif /* SD_STRESS_DEMO_H_ */
