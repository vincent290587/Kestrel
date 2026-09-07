#ifndef GPS_SIM_DEMO_H_
#define GPS_SIM_DEMO_H_

/* Replays the embedded GPX route (gps_sim_route.h) into Locator at ~1Hz --
 * lab GPS testing without real sky visibility. Does not touch or disturb
 * uart_demo()'s real GPS UART path; this is a separate, opt-in override.
 * Started/stopped via cmd_console.c's "SIM START"/"SIM STOP" commands
 * (reachable over both RTT and USB CDC-ACM -- previously RTT-only, a
 * workaround for a since-fixed host ModemManager issue, not a deliberate
 * design; see cmd_console.c). No init call needed -- the replay work item
 * is statically defined and does nothing until gps_sim_route_start(). */
void gps_sim_route_start(void);
void gps_sim_route_stop(void);

#endif /* GPS_SIM_DEMO_H_ */
