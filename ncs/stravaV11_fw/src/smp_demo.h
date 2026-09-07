#ifndef SMP_DEMO_H_
#define SMP_DEMO_H_

/* Starts BLE advertising for the MCUmgr/SMP DFU transport (image
 * upload + confirm + reset, driven from a host mcumgr/SMP client) --
 * must run after ble_demo_start() (needs bt_enable() already called).
 * Actual image/os management is handled entirely by CONFIG_MCUMGR_GRP_IMG/
 * CONFIG_MCUMGR_GRP_OS + CONFIG_MCUMGR_TRANSPORT_BT -- this file only
 * owns advertising so the transport is reachable at all. */
void smp_demo_start(void);

/* On-demand introspection (cmd_console.c's "BLE STATUS") -- boot-time
 * advertising-start log lines have proven unreliable to capture via RTT
 * on this board (they land in the same congested burst as BT/USB init,
 * where CONFIG_SEGGER_RTT_MODE_NO_BLOCK_SKIP can silently drop them), so
 * this lets the current state be checked well after boot instead. */
void smp_demo_log_status(void);

#endif /* SMP_DEMO_H_ */
