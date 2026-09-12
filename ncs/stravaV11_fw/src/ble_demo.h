#ifndef BLE_DEMO_H_
#define BLE_DEMO_H_

#include <stdint.h>

void ble_demo_start(void);

/* On-demand introspection (cmd_console.c's "CPS STATUS") -- one-shot
 * connection/discovery log lines have proven unreliable to catch via
 * RTT on this board (they land in the same congested early-boot burst
 * CONFIG_SEGGER_RTT_MODE_NO_BLOCK_SKIP can silently drop messages in),
 * so this lets the current state be checked well after boot instead. */
void ble_demo_log_status(void);

/* Manual-lap-feature follow-up (2026-09-12): power_provider.c/hrm_provider.c
 * unify these with fec_demo.h/hrm_demo.h's ANT+ equivalents -- same raw
 * value + genuine last-update age shape as fec_demo_get_power_w()/
 * fec_demo_get_power_age_ms() (see that header's own comment for why age,
 * not just a "discovered" flag, is what a freshness check actually needs).
 * UINT32_MAX from either _age_ms() getter means "never received one". */
uint16_t ble_demo_get_power_w(void);
uint32_t ble_demo_get_power_age_ms(void);

uint8_t ble_demo_get_hr_bpm(void);
uint32_t ble_demo_get_hr_age_ms(void);

/* Derived from Cycling Power Measurement's cumulative-crank-revolution
 * delta, not transmitted directly -- see ble_demo.c's own comment. Same
 * shape/convention as the power/HR getters above. */
uint8_t ble_demo_get_cadence_rpm(void);
uint32_t ble_demo_get_cadence_age_ms(void);

#endif /* BLE_DEMO_H_ */
