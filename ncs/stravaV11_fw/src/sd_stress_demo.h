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
 * CONFIG_FAT_FILESYSTEM_ELM isn't set.
 *
 * Not auto-started at boot -- gated behind usb_cmd_demo.c's "STRESS
 * START"/"STRESS STOP" commands instead, since this cycle competes for
 * the same physical SD card as USB MSC (see usb_demo.c's own comment on
 * that hazard): a real host-side bulk file copy over MSC was observed
 * to slow to a crawl with this running concurrently at 500ms cadence.
 */
void sd_stress_demo_start(void);

/* Stops the stress cycle after its current in-flight mount/write/read
 * finishes (no abrupt mid-transaction cancel) -- safe to call whether or
 * not it's currently running. */
void sd_stress_demo_stop(void);

#endif /* SD_STRESS_DEMO_H_ */
