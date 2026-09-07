#ifndef DISK_RAW_TEST_H_
#define DISK_RAW_TEST_H_

/* One-shot raw disk_access write/read round trip against "SD" and "NOR"
 * (implemented in main.c, next to storage_demo()). Not run automatically
 * at boot -- the "SD" half lands on sector 0, the FAT boot sector, and
 * doing that on every boot was silently wiping persistent map-tile data
 * (CONFIG_FS_FATFS_MOUNT_MKFS reformats on the next mount once the boot
 * sector looks invalid). Gated behind cmd_console.c's "DISK TEST"
 * command instead -- run it deliberately, not as part of normal boot. */
void disk_raw_test_start(void);

#endif /* DISK_RAW_TEST_H_ */
