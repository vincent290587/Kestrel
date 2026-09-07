#ifndef USB_CMD_DEMO_H_
#define USB_CMD_DEMO_H_

/* USB CDC-ACM command console: gates sd_stress_demo.c, map_screen_demo.c,
 * disk_raw_test_start(), and sd_format_start() behind explicit "STRESS
 * START"/"STRESS STOP"/"MAP START"/"MAP STOP"/"DISK TEST"/"FORMAT SD"
 * commands instead of auto-starting/auto-running them at boot -- see
 * usb_cmd_demo.c's own comment for why. Call after usb_demo_start() (needs
 * the CDC-ACM device already initialized). */
void usb_cmd_demo_start(void);

#endif /* USB_CMD_DEMO_H_ */
