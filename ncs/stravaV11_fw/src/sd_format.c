#include "sd_format.h"

#if defined(CONFIG_FAT_FILESYSTEM_ELM)
#include <zephyr/fs/fs.h>
#include <ff.h>
#endif

#include <zephyr/sys/printk.h>

#if defined(CONFIG_FAT_FILESYSTEM_ELM)

void sd_format_start(void)
{
	/* fs_mkfs()'s dev_id for FAT goes straight to the underlying ChaN
	 * FatFs f_mkfs() as a raw (char *) -- confirmed by reading
	 * subsys/fs/fat_fs.c's fatfs_mkfs() directly, it does NOT go through
	 * translate_path() the way fs_mount()'s "/SD:" mnt_point does. So
	 * this needs the native FatFs drive string "SD:" (no leading slash),
	 * not "/SD:" (the Zephyr VFS mount point used elsewhere in this
	 * port) and not "SD" (the plain disk_access driver name) -- matches
	 * zephyr/samples/subsys/fs/format's own "RAM:" convention. */
	printk("sd_format: fs_mkfs(\"SD:\") starting -- this destroys all data on the card\n");

	int err = fs_mkfs(FS_FATFS, (uintptr_t)"SD:", NULL, 0);

	printk("sd_format: fs_mkfs(\"SD:\") -> %d\n", err);
}

#else /* !CONFIG_FAT_FILESYSTEM_ELM */

void sd_format_start(void)
{
	printk("sd_format: skipped (CONFIG_FAT_FILESYSTEM_ELM not enabled)\n");
}

#endif
