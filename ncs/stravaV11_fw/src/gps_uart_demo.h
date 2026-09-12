#ifndef GPS_UART_DEMO_H_
#define GPS_UART_DEMO_H_

/*
 * Real GPS module UART reader (2026-09-12) -- continuous, replacing what
 * used to be a one-shot 2-second boot-time capture (main.c's old
 * uart_demo()). Call once from main(), after gps_demo_init(). Every
 * received byte feeds locator_encode_char() (the same shared Locator
 * gps_sim_demo.c's simulated NMEA also feeds -- see gps_uart_demo.c's own
 * comment for why the two are mutually exclusive, not merged behind a
 * selecting "provider" the way power/HRM/cadence are).
 */
void gps_uart_demo_start(void);

/* gps_sim_demo.c calls these around its own start/stop -- pausing stops
 * forwarding bytes to locator_encode_char() (the real module can keep
 * transmitting; those bytes are simply dropped) so simulated and real
 * NMEA sentences never interleave into the one shared parser. */
void gps_uart_demo_pause(void);
void gps_uart_demo_resume(void);

#endif /* GPS_UART_DEMO_H_ */
