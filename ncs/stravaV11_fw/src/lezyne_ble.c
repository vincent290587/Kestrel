#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/mgmt/mcumgr/transport/smp_bt.h>
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
 * unit by this advertised local name. This is the device's one shared
 * connectable identity (see this file's own history in git log /
 * smp_demo.c's top comment: an earlier version gave the Lezyne feature
 * its own independent extended-advertising set alongside smp_demo.c's
 * legacy one, but that cost more flash than this board's thin partition
 * margin could absorb) -- an inbound connection here could be either a
 * Lezyne phone (writes to the Lezyne RX characteristic below) or an
 * mcumgr/SMP host client (uses the separately-registered SMP GATT service
 * on the same connection); both are reachable through this one advertiser.
 */
#define LEZYNE_DEVICE_NAME "LE GPS 12"

/* 2026-09-11: replaced Zephyr's canned NUS service (nrf/subsys/bluetooth/
 * services/nus.c) with a hand-rolled GATT service using the REAL UUIDs a
 * genuine Lezyne GPS unit exposes -- decompiled straight from the actual
 * "GPS Ally" Android app's own BLE client
 * (com.lezyne.gpsally.services.LezyneCycleComputerDevice, see
 * Lezyne_app.md/LezyneCycleComputerDevice.txt at the repo root). The NUS
 * choice was always an unverified guess (this file's own prior comment
 * said as much) and it was wrong: GPS Ally's onServicesDiscovered()
 * walks every GATT service the peer exposes looking specifically for
 * service UUID 904d0001-2ce9-078d-944d-263fd93d95b2 ("z" in that code) --
 * if it isn't found, GPS Ally logs "no data service" and disconnects
 * immediately, no NUS fallback exists. The two data characteristics
 * (writeable from the phone / notified to the phone) are 904d0002 and
 * 904d0003 respectively -- receiveDataCharacteristic()/
 * transmitDataCharacteristic() in the decompiled source, matching what
 * l_protocol.h's IncomingCommands/OutgoingCommands enums already assumed
 * as the wire format riding on top.
 *
 * GPS Ally's onServicesDiscovered() ALSO requires the standard BLE
 * Location and Navigation service (0x1819, LOCATION_AND_NAVIGATION in the
 * decompiled source) with its Location and Speed characteristic (0x2A67)
 * to be present ("z2" in that code) -- missing either service (not just
 * the Lezyne one) makes it disconnect right after discovery ("no location
 * service"). This port doesn't implement live-tracking data on that
 * characteristic (out of scope, same as segments/navigation -- see
 * lezyne_handler.c's own top comment); it exists here purely so GPS
 * Ally's service-discovery gate passes and the connection survives.
 */
#define LEZYNE_SVC_VAL BT_UUID_128_ENCODE(0x904d0001, 0x2ce9, 0x078d, 0x944d, 0x263fd93d95b2)
#define LEZYNE_RX_VAL BT_UUID_128_ENCODE(0x904d0002, 0x2ce9, 0x078d, 0x944d, 0x263fd93d95b2)
#define LEZYNE_TX_VAL BT_UUID_128_ENCODE(0x904d0003, 0x2ce9, 0x078d, 0x944d, 0x263fd93d95b2)

static const struct bt_uuid_128 lez_svc_uuid = BT_UUID_INIT_128(LEZYNE_SVC_VAL);
static const struct bt_uuid_128 lez_rx_uuid = BT_UUID_INIT_128(LEZYNE_RX_VAL);
static const struct bt_uuid_128 lez_tx_uuid = BT_UUID_INIT_128(LEZYNE_TX_VAL);

static struct bt_conn *m_conn;
static bool m_tx_notify_enabled;
static uint32_t m_adv_start_attempts;
static int m_last_adv_err = -EAGAIN;

static ssize_t lez_write_rx(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			     const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(offset);
	ARG_UNUSED(flags);

	lezyne_handler_on_rx(buf, len);
	return len;
}

static void lez_tx_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);

	m_tx_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
}

/* Attribute layout (indices matter -- lezyne_ble_send() below notifies
 * attrs[2] by hand, same pattern nrf/subsys/bluetooth/services/nus.c uses
 * for its own TX characteristic): 0=primary service, 1=TX chrc
 * declaration, 2=TX value, 3=CCC, 4=RX chrc declaration, 5=RX value. */
BT_GATT_SERVICE_DEFINE(lez_svc, BT_GATT_PRIMARY_SERVICE(&lez_svc_uuid.uuid),
			BT_GATT_CHARACTERISTIC(&lez_tx_uuid.uuid, BT_GATT_CHRC_NOTIFY,
						BT_GATT_PERM_NONE, NULL, NULL, NULL),
			BT_GATT_CCC(lez_tx_ccc_cfg_changed,
				    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
			BT_GATT_CHARACTERISTIC(&lez_rx_uuid.uuid,
						BT_GATT_CHRC_WRITE |
							BT_GATT_CHRC_WRITE_WITHOUT_RESP,
						BT_GATT_PERM_WRITE, NULL, lez_write_rx, NULL), );

/* Standard Location and Navigation service (0x1819) + Location and Speed
 * characteristic (0x2A67) -- present purely to satisfy GPS Ally's
 * discovery gate (see this file's top comment), no real data backs it. */
static void lez_lns_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	ARG_UNUSED(value);
}

BT_GATT_SERVICE_DEFINE(lez_lns_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_LNS),
			BT_GATT_CHARACTERISTIC(BT_UUID_GATT_LOC_SPD, BT_GATT_CHRC_NOTIFY,
						BT_GATT_PERM_NONE, NULL, NULL, NULL),
			BT_GATT_CCC(lez_lns_ccc_cfg_changed,
				    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE), );

/* Primary AD: flags + the real Lezyne data service UUID (what GPS Ally's
 * own scan/discovery actually looks for -- see this file's top comment).
 * Scan response: device name + SMP service UUID (what an mcumgr/SMP host
 * client filters on) -- split across both because a 128-bit UUID is 18
 * bytes AD-encoded and legacy AD/SD are each capped at 31 bytes;
 * flags(3) + Lezyne(18) = 21 fits primary, name(11) + SMP(18) = 29 fits
 * scan response. Any real GATT client discovers every service the peer
 * exposes regardless of which UUID happened to be advertised, so this
 * split has no functional effect beyond which UUID a scanner's own
 * *filter* can match on before connecting. */
static const struct bt_data lez_ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, LEZYNE_SVC_VAL),
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

	LOG_INF("Lezyne advertising started as \"%s\" (Lezyne + SMP UUIDs)", LEZYNE_DEVICE_NAME);
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
	m_tx_notify_enabled = false;
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
	lezyne_handler_init();

	k_work_schedule(&lez_adv_start_work, K_NO_WAIT);
}

int lezyne_ble_send(const uint8_t *data, uint16_t len)
{
	if (!m_conn) {
		return -ENOTCONN;
	}

	if (!m_tx_notify_enabled || !bt_gatt_is_subscribed(m_conn, &lez_svc.attrs[2],
							     BT_GATT_CCC_NOTIFY)) {
		/* Same transient-backpressure contract bt_nus_send() gave
		 * lez_tick_handler() before this rewrite (see its own
		 * comment): completely normal for the first tick or two
		 * right after connect, before the peer's CCCD write lands. */
		return -EINVAL;
	}

	struct bt_gatt_notify_params params = {
		.attr = &lez_svc.attrs[2],
		.data = data,
		.len = len,
	};

	return bt_gatt_notify_cb(m_conn, &params);
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

	/* Max notification payload per ATT 3.4.7.1: ATT_MTU - 3 -- same
	 * computation bt_nus_get_mtu() did (nrf/include/bluetooth/services/
	 * nus.h), inlined now that this file no longer depends on NUS. */
	return bt_gatt_get_mtu(m_conn) - 3;
}

void lezyne_ble_log_status(void)
{
	/* WRN, not INF: this module is registered at LOG_LEVEL_WRN (see the
	 * top of this file), which compiles LOG_INF out entirely -- found on
	 * real hardware alongside the identical bug in
	 * lezyne_handler_log_status(), "LEZ STATUS" was silently printing
	 * nothing here either despite this function running. */
	LOG_WRN("lezyne_ble: connected=%d tx_notify_enabled=%d adv_attempts=%u last_adv_err=%d",
		m_conn != NULL, m_tx_notify_enabled, m_adv_start_attempts, m_last_adv_err);
}
