#ifndef MAP_SCREEN_DEMO_H_
#define MAP_SCREEN_DEMO_H_

/* Live GPS-driven map screen: seeds real_route_tiles.h's real
 * OpenStreetMap data onto the SD card once at start, then periodically
 * (~1Hz, matching gps_sim_demo.c's replay rate) re-centers and redraws
 * the map on the current GPS fix, loading whichever tile the position
 * currently falls into via a real fs_open()/fs_read() SD-card round trip
 * and switching tiles correctly if/when it crosses a boundary. No-ops
 * cleanly if CONFIG_FAT_FILESYSTEM_ELM isn't set. Call after
 * storage_demo() and gps_demo_init().
 *
 * Not auto-started at boot -- gated behind usb_cmd_demo.c's "MAP
 * START"/"MAP STOP" commands, same reasoning as sd_stress_demo.h: real
 * SD-card I/O every redraw cycle competes with USB MSC host access. In
 * practice this demo is SD-quiet until gps_sim_demo also has a fix to
 * report (its work handler returns before touching the card otherwise),
 * but explicit gating keeps it from depending on that as its only
 * safeguard. */
void map_screen_demo_start(void);

/* Stops the redraw loop after its current in-flight cycle finishes --
 * safe to call whether or not it's currently running. */
void map_screen_demo_stop(void);

#endif /* MAP_SCREEN_DEMO_H_ */
