#ifndef SENSOR_SCREEN_DEMO_H_
#define SENSOR_SCREEN_DEMO_H_

/* Call after hrm_demo_start()/bsc_demo_start() -- periodically (see
 * sensor_screen_demo.c) redraws the display with their latest values via
 * gfx_demo_show_sensors(). Runs indefinitely, unlike this port's other
 * demos' fixed-iteration smoke tests: a live data screen is the actual
 * intended behavior here, not just a one-shot hardware proof. */
void sensor_screen_demo_start(void);

#endif /* SENSOR_SCREEN_DEMO_H_ */
