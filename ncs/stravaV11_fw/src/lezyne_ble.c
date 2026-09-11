#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/mgmt/mcumgr/transport/smp_bt.h>
#include <bluetooth/services/nus.h>
#include <zephyr/logging/log.h>

#include "lezyne_ble.h"
#include "lezyne_handler.h"

/* WRN, not INF -- see lezyne_handler.c's identical comment; this file's
 * own connect/disconnect/advertise narration cost real flash bytes this
 * board's thin partition margin couldn't spare. "LEZ STATUS"
 * (cmd_console.c) covers the on-demand introspection case instead. */
LOG_MODULE_REGISTER(lezyne_ble, LOG_LEVEL_WRN);

/* Real Lezyne product naming (GpsDeviceModel::Y12Mega/Y12MegaColor in
 * l_protocol.h) -- the phone-side "GPS Ally" app identifies a real Lezyne
 * unit by this advertised local name. This is now the device's one
 * shared connectable identity (see this file's own history in git log /
 * smp_demo.c's top comment: an earlier version gave the Lezyne feature
 * its own independent extended-advertising set alongside smp_demo.c's
 * legacy one, but that cost more flash than this board's thin partition
 * margin could absorb) -- an inbound connection here could be either a
 * Lezyne phone (writes to the NUS RX characteristic) or an mcumgr/SMP
 * host client (uses the separately-registered SMP GATT service on the
 * same connection); both are reachable through this one advertiser. */
#define LEZYNE_DEVICE_NAME "LE GPS 12"

static struct bt_conn *m_conn;
static uint32_t m_adv_start_attempts;
static int m_last_adv_err = -EAGAIN;

static void lez_nus_received(struct bt_conn *conn, const uint8_t *const data, uint16_t len)
{
	ARG_UNUSED(conn);
	lezyne_handler_on_rx(data, len);
}

static struct bt_nus_cb lez_nus_cb = {
	.received = lez_nus_received,
};

/* Primary AD: flags + NUS service UUID (what a Lezyne GPS Ally scan
 * filters on). Scan response: device name + SMP service UUID (what an
 * mcumgr/SMP host client filters on) -- split across both because a
 * 128-bit UUID is 18 bytes AD-encoded and legacy AD/SD are each capped at
 * 31 bytes; flags(3) + NUS(18) = 21 fits primary, name(11) + SMP(18) = 29
 * fits scan response. Any real GATT client discovers every service the
 * peer exposes regardless of which UUID happened to be advertised, so
 * this split has no functional effect beyond which UUID a scanner's own
 * *filter* can match on before connecting. */
static const struct bt_data lez_ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_VAL),
};

static const struct bt_data lez_sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, LEZYNE_DEVICE_NAME, sizeof(LEZYNE_DEVICE_NAME) - 1),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, SMP_BT_SVC_UUID_VAL),
};

/* USE_IDENTITY: the existing central-role scan (ble_demo.c) runs
 * continuously, and the Bluetooth Core Spec forbids changing the random
 * address while scanning is active (Vol 4, Part E 7.8.4).
 * CONFIG_BT_SCAN_WITH_IDENTITY makes the scanner use the identity address
 * too, so this advertiser matching it hits Zephyr's already-set-to-this-
 * value shortcut instead of issuing the forbidden HCI command. */
static const struct bt_le_adv_param lez_adv_param =
	BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_USE_IDENTITY,
			      BT_GAP_ADV_FAST_INT_MIN_2, BT_GAP_ADV_FAST_INT_MAX_2, NULL);

static void lez_adv_start_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(lez_adv_start_work, lez_adv_start_work_handler);

static void lez_adv_start_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	m_adv_start_attempts++;
	m_last_adv_err =
		bt_le_adv_start(&lez_adv_param, lez_ad, ARRAY_SIZE(lez_ad), lez_sd, ARRAY_SIZE(lez_sd));

	if (m_last_adv_err) {
		LOG_ERR("Lezyne (\"%s\") advertising failed to start (err %d)", LEZYNE_DEVICE_NAME,
			m_last_adv_err);
		return;
	}

	LOG_INF("Lezyne advertising started as \"%s\" (NUS + SMP UUIDs)", LEZYNE_DEVICE_NAME);
}

static void lez_conn_connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		return;
	}

	struct bt_conn_info info;

	if (bt_conn_get_info(conn, &info) != 0 || info.role != BT_CONN_ROLE_PERIPHERAL) {
		/* Not our (inbound peripheral-role) connection -- ble_demo.c's
		 * own central-role connections are handled entirely there. */
		return;
	}

	m_conn = conn;
	LOG_INF("Lezyne/SMP peer connected");
	lezyne_handler_on_connected();
}

static void lez_conn_disconnected(struct bt_conn *conn, uint8_t reason)
{
	if (conn != m_conn) {
		return;
	}

	LOG_INF("Lezyne/SMP peer disconnected (reason 0x%02x)", reason);
	m_conn = NULL;
	lezyne_handler_on_disconnected();

	/* Re-arm advertising so a phone/DFU host can reconnect. */
	k_work_schedule(&lez_adv_start_work, K_NO_WAIT);
}

BT_CONN_CB_DEFINE(lez_conn_callbacks) = {
	.connected = lez_conn_connected,
	.disconnected = lez_conn_disconnected,
};

void lezyne_ble_start(void)
{
	int err = bt_nus_init(&lez_nus_cb);

	if (err) {
		LOG_ERR("bt_nus_init() failed (err %d)", err);
		return;
	}

	lezyne_handler_init();

	k_work_schedule(&lez_adv_start_work, K_NO_WAIT);
}

int lezyne_ble_send(const uint8_t *data, uint16_t len)
{
	if (!m_conn) {
		return -ENOTCONN;
	}

	return bt_nus_send(m_conn, data, len);
}

bool lezyne_ble_is_connected(void)
{
	return m_conn != NULL;
}

uint16_t lezyne_ble_get_mtu(void)
{
	if (!m_conn) {
		return 20; /* stravaV10's own BLE_NUS_STD_DATA_LEN default */
	}

	return (uint16_t)bt_nus_get_mtu(m_conn);
}

void lezyne_ble_log_status(void)
{
	/* WRN, not INF: this module is registered at LOG_LEVEL_WRN (see the
	 * top of this file), which compiles LOG_INF out entirely -- found on
	 * real hardware alongside the identical bug in
	 * lezyne_handler_log_status(), "LEZ STATUS" was silently printing
	 * nothing here either despite this function running. */
	LOG_WRN("lezyne_ble: connected=%d adv_attempts=%u last_adv_err=%d", m_conn != NULL,
		m_adv_start_attempts, m_last_adv_err);
}
