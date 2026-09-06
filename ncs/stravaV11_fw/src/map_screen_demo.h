#ifndef MAP_SCREEN_DEMO_H_
#define MAP_SCREEN_DEMO_H_

/* Live GPS-driven map screen -- SD-card-free (see map_screen_demo.c's
 * top comment for why): renders directly from real_route_tiles.h, real
 * OpenStreetMap data linked into flash, no SD card or filesystem
 * involved at all. Periodically (~1Hz, matching gps_sim_demo.c's replay
 * rate) re-centers and redraws the map on the current GPS fix, picking
 * whichever embedded tile the position currently falls into and
 * switching tiles correctly if/when it crosses a boundary. Call after
 * gps_demo_init(). */
void map_screen_demo_start(void);

#endif /* MAP_SCREEN_DEMO_H_ */
