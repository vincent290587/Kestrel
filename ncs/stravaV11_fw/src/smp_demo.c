/*
 * MCUmgr/SMP-over-BLE DFU transport -- the real update-cycle todo item
 * flagged since Phase 10 ("MCUboot itself boots the signed app correctly,
 * but an actual over-the-air update has never been attempted"). Actual
 * image upload/list/confirm and reset handling is done entirely by
 * Zephyr's own CONFIG_MCUMGR_GRP_IMG/CONFIG_MCUMGR_GRP_OS + the BT
 * transport (CONFIG_MCUMGR_TRANSPORT_BT) -- none of that is this port's
 * code. This file only owns advertising, following the closest upstream
 * reference (zephyr/samples/subsys/mgmt/mcumgr/smp_svr's src/bluetooth.c)
 * but adapted for this app's existing BLE setup: that sample calls
 * bt_enable() itself, since it has no other BLE role; this app already
 * calls bt_enable() in ble_demo_start() (for the existing central-role
 * CP/HRS scanning), so smp_demo_start() just adds advertising on top of
 * the BT stack ble_demo_start() already brought up -- call this after
 * ble_demo_start() returns.
 *
 * Advertises the SMP service UUID (SMP_BT_SVC_UUID_VAL, from
 * zephyr/include/zephyr/mgmt/mcumgr/transport/smp_bt.h) so an mcumgr/SMP
 * host client can find and connect to this device by service UUID rather
 * than needing to already know its address.
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/mgmt/mcumgr/transport/smp_bt.h>
#include <zephyr/logging/log.h>

#include "smp_demo.h"

LOG_MODULE_REGISTER(smp_demo, LOG_LEVEL_INF);

static struct k_work advertise_work;
static int m_last_adv_err = -EAGAIN; /* not attempted yet */
static uint32_t m_adv_attempts;

/* Found on real hardware: BT_LE_ADV_CONN_FAST_1 (used by this file's
 * upstream reference, smp_svr's bluetooth.c) has no identity-address
 * option, so bt_le_adv_start() tries to generate and set a fresh private
 * address every call -- forbidden by the Bluetooth Core Spec (Vol 4,
 * Part E 7.8.4) while legacy scanning is active, which ble_demo.c's
 * central role does continuously. That sample never hits this because
 * it has no other BLE role; this app does, so BT_LE_ADV_OPT_USE_IDENTITY
 * is added here to advertise using the identity address bt_enable()
 * already set once at boot (no HCI address-change command needed at
 * all, so the scanning-vs-address-change conflict never arises). */
static const struct bt_le_adv_param smp_adv_param =
	BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_USE_IDENTITY,
			     BT_GAP_ADV_FAST_INT_MIN_1, BT_GAP_ADV_FAST_INT_MAX_1, NULL);

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, SMP_BT_SVC_UUID_VAL),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void advertise(struct k_work *work)
{
	ARG_UNUSED(work);

	m_adv_attempts++;
	m_last_adv_err = bt_le_adv_start(&smp_adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

	if (m_last_adv_err) {
		LOG_ERR("SMP advertising failed to start (err %d)", m_last_adv_err);
		return;
	}

	LOG_INF("SMP advertising started (service UUID visible to mcumgr/SMP host clients)");
}

static void recycled(void)
{
	/* Only restarts SMP advertising -- ble_demo.c's own central-role
	 * scan restart (on its own connection's disconnect) is unaffected,
	 * each BT_CONN_CB_DEFINE registration only reacts to its own
	 * concerns. */
	k_work_submit(&advertise_work);
}

BT_CONN_CB_DEFINE(smp_conn_callbacks) = {
	.recycled = recycled,
};

void smp_demo_start(void)
{
	k_work_init(&advertise_work, advertise);
	k_work_submit(&advertise_work);
}

void smp_demo_log_status(void)
{
	LOG_INF("smp_demo: %u advertise attempt(s), last result %d (%s)", m_adv_attempts,
		m_last_adv_err, m_last_adv_err == 0 ? "advertising" : "not advertising");
}
