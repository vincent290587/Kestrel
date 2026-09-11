/*
 * MCUmgr/SMP-over-BLE DFU transport -- the real update-cycle todo item
 * flagged since Phase 10 ("MCUboot itself boots the signed app correctly,
 * but an actual over-the-air update has never been attempted"). Actual
 * image upload/list/confirm and reset handling is done entirely by
 * Zephyr's own CONFIG_MCUMGR_GRP_IMG/CONFIG_MCUMGR_GRP_OS + the BT
 * transport (CONFIG_MCUMGR_TRANSPORT_BT) -- none of that is this port's
 * code, and the SMP GATT service itself is auto-registered by that
 * Kconfig regardless of what advertises for it.
 *
 * Update 2026-09-11 (Lezyne feature): this file used to own its own
 * connectable advertising (BT_LE_ADV_OPT_CONN, SMP service UUID). That
 * stopped being viable once the Lezyne feature needed ITS OWN
 * connectable, independently-named ("LE GPS 12") advertisement for the
 * real Lezyne "GPS Ally" phone app to find this device by -- legacy
 * advertising only supports one active advertiser, and a real-hardware
 * flash-overflow test (the signed app image landed ~4.4KB over
 * app_max_size() once both an extended-advertising second adv set AND
 * this file's own legacy one were both in the image) showed that adding
 * CONFIG_BT_EXT_ADV just to run two independent advertisers concurrently
 * cost more flash than this board's thin remaining partition margin
 * could absorb (see pm_static_stravav11_nrf52840.yml's own comment on
 * why mcuboot's partition is already at its real minimum -- there is no
 * further slack to grow into). So: lezyne_ble.c now owns the ONE shared
 * legacy advertiser, and its own advertising data includes the SMP
 * service UUID (SMP_BT_SVC_UUID_VAL) in its scan response specifically
 * so an mcumgr/SMP host client can still find this device by UUID -- see
 * that file's own top-of-file comment for the full reasoning. This file
 * no longer advertises anything; it's kept only because
 * smp_demo_log_status() is still a useful, separately-named "is DFU
 * actually reachable" check from cmd_console.c's "BLE STATUS" command. */

#include <zephyr/logging/log.h>

#include "smp_demo.h"
#include "lezyne_ble.h"

LOG_MODULE_REGISTER(smp_demo, LOG_LEVEL_INF);

void smp_demo_start(void)
{
	/* No-op: see this file's top comment -- lezyne_ble.c now owns the
	 * one shared advertiser, whose data includes the SMP service UUID. */
}

void smp_demo_log_status(void)
{
	LOG_INF("smp_demo: SMP transport is reachable via lezyne_ble's shared "
		"advertiser (connected=%d) -- see that file, this one no longer "
		"advertises on its own",
		lezyne_ble_is_connected());
}
