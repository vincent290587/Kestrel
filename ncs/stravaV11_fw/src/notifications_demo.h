#ifndef NOTIFICATIONS_DEMO_H_
#define NOTIFICATIONS_DEMO_H_

/* Wires up the ported WS2812 status-LED code (lib/source/display/
 * notifications.c + adapters/drv_ws2812.c): notifications_init(), the
 * same one-shot red "boot flash" stravaV10's own main.cpp fires at
 * startup, then a periodic notifications_tasks() poll every 50ms
 * (stravaV10's Model.cpp calls it from peripherals_task at the same
 * cadence) for as long as the device runs -- this is the status LED, not
 * a bounded smoke test. Call after usb_demo_start() isn't required; no
 * dependency on USB. Real segment-race triggers (notifications_segNotify()
 * from SegmentManager/BoucleCRS) aren't wired to anything live yet --
 * Boucle*.cpp isn't ported into stravaV11_fw, so beyond the boot flash
 * this will just show the LED off. */
void notifications_demo_start(void);

#endif /* NOTIFICATIONS_DEMO_H_ */
