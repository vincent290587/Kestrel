#ifndef GPS_SIM_DEMO_H_
#define GPS_SIM_DEMO_H_

/* Call after gps_demo_init(). Listens for "SIM START"/"SIM STOP" lines over
 * the RTT down channel 0 (the same "Terminal" pair used for console/log
 * output -- see gps_sim_demo.c for why RTT instead of USB CDC-ACM) and,
 * while active, replays the embedded GPX route (gps_sim_route.h) into
 * Locator at ~1Hz -- lab GPS testing without real sky visibility. Does not
 * touch or disturb uart_demo()'s real GPS UART path; this is a separate,
 * opt-in override. */
void gps_sim_demo_start(void);

#endif /* GPS_SIM_DEMO_H_ */
