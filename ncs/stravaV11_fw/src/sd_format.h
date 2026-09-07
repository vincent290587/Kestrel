#ifndef SD_FORMAT_H_
#define SD_FORMAT_H_

/* Deliberately, cleanly reformats the SD card's FAT filesystem on demand
 * via fs_mkfs() -- gated behind cmd_console.c's "FORMAT SD" command, not
 * run automatically. Added after a real corruption incident: a large
 * host-side USB MSC file copy followed immediately by a board reset (via
 * nrfutil device recover/reset during unrelated SWD/RTT debugging) left
 * the card's FAT table inconsistent ("fat_free_clusters: deleting FAT
 * entry beyond EOF" from the host kernel, filesystem forced read-only) --
 * host-side `sync` only flushes the host's own page cache, not any
 * write-back still in flight on the device/SD-card side, so a reset
 * shortly after a big write is genuinely risky. This gives a deliberate,
 * one-shot way to get back to a known-good filesystem without needing
 * host-side root (mkfs.vfat needs sudo, which isn't available/appropriate
 * to script) or relying on the old accidental mechanism (corrupting
 * sector 0 so CONFIG_FS_FATFS_MOUNT_MKFS reformats on the next boot,
 * removed for exactly that "silently wipes real data" reason -- see
 * disk_raw_test.h). Destroys all data currently on the card. */
void sd_format_start(void);

#endif /* SD_FORMAT_H_ */
