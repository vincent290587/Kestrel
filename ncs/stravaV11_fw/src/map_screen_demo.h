#ifndef MAP_SCREEN_DEMO_H_
#define MAP_SCREEN_DEMO_H_

/* Live GPS-driven map screen: seeds real_route_tiles.h's real
 * OpenStreetMap data onto the SD card once at start (still the only way
 * to get files onto it, no USB MSC), then periodically (~1Hz, matching
 * gps_sim_demo.c's replay rate) re-centers and redraws the map on the
 * current GPS fix, loading whichever tile the position currently falls
 * into via a real fs_open()/fs_read() SD-card round trip and switching
 * tiles correctly if/when it crosses a boundary. No-ops cleanly if
 * CONFIG_FAT_FILESYSTEM_ELM isn't set. Call after storage_demo() and
 * gps_demo_init(). */
void map_screen_demo_start(void);

#endif /* MAP_SCREEN_DEMO_H_ */
