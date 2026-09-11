#ifndef SMP_DEMO_H_
#define SMP_DEMO_H_

/* No-op as of the Lezyne feature (2026-09-11) -- lezyne_ble.c now owns the
 * one shared legacy advertiser (whose data includes the SMP service UUID),
 * see smp_demo.c's own top-of-file comment for why. Kept as a stable call
 * site in main.c rather than removed, in case a future board wants an
 * independent SMP-only advertiser again. */
void smp_demo_start(void);

/* On-demand introspection (cmd_console.c's "BLE STATUS") -- boot-time
 * advertising-start log lines have proven unreliable to capture via RTT
 * on this board (they land in the same congested burst as BT/USB init,
 * where CONFIG_SEGGER_RTT_MODE_NO_BLOCK_SKIP can silently drop them), so
 * this lets the current state be checked well after boot instead. */
void smp_demo_log_status(void);

#endif /* SMP_DEMO_H_ */
