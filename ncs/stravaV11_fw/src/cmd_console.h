#ifndef CMD_CONSOLE_H_
#define CMD_CONSOLE_H_

/* Single command listener/dispatcher, reachable over BOTH the RTT down
 * channel 0 and USB CDC-ACM at once -- one shared command table ("SIM
 * START"/"SIM STOP"/"STRESS START"/"STRESS STOP"/"MAP START"/"MAP
 * STOP"/"DISK TEST"/"FORMAT SD"), not two separate implementations. This
 * replaces what used to be two independent listeners with duplicated
 * line-buffering logic: gps_sim_demo.c's own RTT-only "SIM START"/"SIM
 * STOP" handling (a workaround for a since-fixed host ModemManager issue
 * that made USB CDC-ACM impractical at the time it was written) and
 * usb_cmd_demo.c's CDC-ACM-only handling for everything else -- an
 * accident of build order, not a deliberate design; every command should
 * reach the firmware the same way regardless of which transport happens
 * to be connected.
 *
 * Call after usb_demo_start() (needs the CDC-ACM device already
 * initialized), gps_demo_init() (SIM START touches Locator via
 * gps_sim_demo), and ant_dm_demo_start() ("DM SEARCH"/"DM LIST"/"DM
 * PICK"/"DM CANCEL" touch the ANT+ device manager). The RTT listener
 * no-ops if CONFIG_USE_SEGGER_RTT isn't set (DK only -- the custom PCB
 * always has it, see gps_sim_demo.c's original comment for why); the
 * CDC-ACM listener always starts. */
void cmd_console_start(void);

#endif /* CMD_CONSOLE_H_ */
