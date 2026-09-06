#ifndef MAP_DEMO_H_
#define MAP_DEMO_H_

/* Maps-feasibility step 3.5 (docs/maps_feasibility.md): the SD-card
 * fs_open()/fs_read() loading glue that map_tile.c's native_sim
 * validation deliberately left undone. Call after storage_demo() (needs
 * CONFIG_FAT_FILESYSTEM_ELM, matching sd_fat_demo()). No-ops cleanly if
 * that Kconfig isn't set. */
void map_demo(void);

#endif /* MAP_DEMO_H_ */
