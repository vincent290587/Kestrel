#ifndef BLE_DEMO_H_
#define BLE_DEMO_H_

void ble_demo_start(void);

/* On-demand introspection (cmd_console.c's "CPS STATUS") -- one-shot
 * connection/discovery log lines have proven unreliable to catch via
 * RTT on this board (they land in the same congested early-boot burst
 * CONFIG_SEGGER_RTT_MODE_NO_BLOCK_SKIP can silently drop messages in),
 * so this lets the current state be checked well after boot instead. */
void ble_demo_log_status(void);

#endif /* BLE_DEMO_H_ */
