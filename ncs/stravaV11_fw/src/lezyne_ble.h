#ifndef LEZYNE_BLE_H_
#define LEZYNE_BLE_H_

#include <stdint.h>
#include <stdbool.h>

/* BLE plumbing for the "Lezyne feature" (see CLAUDE.md/todo.md 2026-09-11
 * entry) -- rewritten against Zephyr's Bluetooth host + the ready-made NUS
 * service (nrf/subsys/bluetooth/services/nus.c), replacing stravaV10's
 * rf/app_ble_peripheral.c (943 lines of SDK16 ble_nus/ble_advertising/
 * peer_manager boilerplate -- none of it portable as-is). Protocol dispatch
 * itself lives in lezyne_handler.{h,c}, mirroring stravaV10's own
 * app_ble_peripheral.c/app_packets_handler.c split.
 *
 * A THIRD concurrent BLE role: this board already connects as central
 * (ble_demo.c, scanning for CP/HRS peripherals since Phase 5) and
 * peripheral (smp_demo.c, MCUmgr/SMP DFU since Phase 10). This file now
 * owns the device's ONE shared connectable advertiser (legacy
 * bt_le_adv_start(), advertising as "LE GPS 12" with both the NUS and SMP
 * service UUIDs) -- an earlier version gave this its own independent
 * extended-advertising set instead (CONFIG_BT_EXT_ADV), but that measurably
 * overflowed this board's already-thin flash partition margin on real
 * hardware, so smp_demo.c's own advertising was folded into this file
 * instead (see smp_demo.c's top comment for the full story). An inbound
 * connection here is filtered to peripheral-role only (`info.role ==
 * BT_CONN_ROLE_PERIPHERAL`), the same pattern ble_demo.c's own central-role
 * connected() callback already used since Phase 10 to ignore this kind of
 * connection.
 */

void lezyne_ble_start(void);

/* Send one NUS TX notification to the connected Lezyne peer. Returns
 * bt_nus_send()'s result (0 on success), or -ENOTCONN if nothing is
 * connected right now. Callers (lezyne_handler.c) are expected to retry
 * on -ENOMEM/-EAGAIN -- the same backpressure stravaV10's own NRF_QUEUE-
 * based retry loop in app_packets_handler.c's _process_tx() handled. */
int lezyne_ble_send(const uint8_t *data, uint16_t len);

bool lezyne_ble_is_connected(void);

/* Max single-notification payload for the current connection's negotiated
 * MTU (bt_nus_get_mtu()), or the safe pre-negotiation default (20, ATT_MTU
 * 23 - 3 bytes ATT header) if nothing is connected yet -- matches
 * stravaV10's own BLE_NUS_STD_DATA_LEN/NUS_LONG_PACKETS_SIZE constants,
 * both 20 in the original SDK16 code (default ATT MTU, never renegotiated
 * up in that firmware). */
uint16_t lezyne_ble_get_mtu(void);

void lezyne_ble_log_status(void);

#endif /* LEZYNE_BLE_H_ */
