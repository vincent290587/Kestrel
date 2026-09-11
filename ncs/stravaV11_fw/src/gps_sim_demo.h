#ifndef GPS_SIM_DEMO_H_
#define GPS_SIM_DEMO_H_

/* Lightweight GPS-fix simulator for lab testing (no real sky visibility
 * needed) -- feeds real, checksummed synthetic NMEA sentences (RMC+GGA)
 * one character at a time through Locator's own locator_encode_char(),
 * the exact same entry point uart_demo() feeds real GPS UART bytes
 * through. Deliberately NOT a route replay from an embedded waypoint
 * array like the original gps_sim_demo.c (removed during the Vue-assembly
 * flash-budget crunch, see cmd_console.c's own history) -- the whole
 * track is computed procedurally each tick (a straight line at a fixed
 * heading/speed from a fixed start point), so this costs a few dozen
 * bytes of state, not a ~33KB embedded array.
 *
 * Real motivation for going through NMEA rather than gps_demo_inject_
 * location() (the position-only injection path that DOES exist, still
 * used by nothing now that the old route-replay demo is gone): found by
 * reading Locator::getFullDateTime() and Locator::tasks() directly --
 * date/time (which "RIDE START"'s own GPS-time-lock guard needs) come
 * from TinyGPS++'s own gps.date/gps.time objects, populated only by
 * parsing real NMEA characters through locator_encode_char() -- injecting
 * gps_loc directly never touched them. A position-only injection could
 * make gps_demo_get_position() succeed while "RIDE START" stayed refused
 * forever. Feeding real sentences sets both at once, correctly, through
 * the same real parsing path a real module uses. */
void gps_sim_demo_start(void);
void gps_sim_demo_stop(void);

#endif /* GPS_SIM_DEMO_H_ */
