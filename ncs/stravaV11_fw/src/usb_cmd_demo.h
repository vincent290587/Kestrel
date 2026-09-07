#ifndef USB_CMD_DEMO_H_
#define USB_CMD_DEMO_H_

/* USB CDC-ACM command console: gates sd_stress_demo.c and
 * map_screen_demo.c behind explicit "STRESS START"/"STRESS STOP"/"MAP
 * START"/"MAP STOP" commands instead of auto-starting them at boot --
 * see usb_cmd_demo.c's own comment for why. Call after usb_demo_start()
 * (needs the CDC-ACM device already initialized). */
void usb_cmd_demo_start(void);

#endif /* USB_CMD_DEMO_H_ */
