/*
 * Phase 5 BLE central bring-up: scans for and connects to a Cycling Power
 * Service (0x1818) peripheral, using NCS's bt_scan + bt_gatt_dm helpers --
 * the same pattern nrf/samples/bluetooth/central_and_peripheral_hrs uses for
 * the standard Heart Rate Service, adapted here for Cycling Power via
 * bt_cp_client (lib/bt_cp_client.c, ported from stravaV10's ble_cp_c).
 *
 * No pairing/bonding: cycling power meters are normally open GATT servers,
 * matching stravaV10's own ble_cp_c usage.
 *
 * Real-hardware caveat: this proves the BLE stack initializes and scanning
 * runs cleanly on the DK's radio. Whether it actually finds and parses data
 * from a real power meter depends on one being nearby and powered on --
 * same "clean init, real validation pending real peripheral hardware" tier
 * as the sensors/display in earlier phases.
 *
 * Phase 11 update: also scans for and connects to a Heart Rate Service
 * (0x180D) peripheral, via NCS's own ready-made bt_hrs_client -- unlike
 * Cycling Power, no porting needed, since bt_hrs_client is exactly the
 * upstream library bt_cp_client was itself modeled on. Validates the real
 * HRM strap's BLE side (it broadcasts both ANT+ and BLE) alongside
 * hrm_demo.c's already-working ANT+ path. Only one bt_conn is tracked
 * (default_conn), matching the existing single-peripheral pattern below --
 * scan_filter_match() records which service (CPS or HRS) matched so
 * connected() knows which single bt_gatt_dm_start() to issue for that
 * connection; supporting simultaneous CPS+HRS peripherals would need
 * CONFIG_BT_MAX_CONN > 1 and per-connection state, out of scope here.
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <bluetooth/gatt_dm.h>
#include <bluetooth/scan.h>
#include <bluetooth/services/hrs_client.h>

#include "bt_cp_client.h"
#include "ble_demo.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ble_demo, LOG_LEVEL_INF);

static struct bt_cp_client cp_c;
static struct bt_hrs_client hrs_c;
static struct bt_conn *default_conn;

enum ble_matched_service {
	BLE_MATCHED_SERVICE_NONE,
	BLE_MATCHED_SERVICE_CPS,
	BLE_MATCHED_SERVICE_HRS,
};

static enum ble_matched_service matched_service;

static bool m_cps_discovered;
static uint32_t m_cps_notif_count;
static int16_t m_last_power_w;

static bool m_hrs_discovered;
static uint32_t m_hrs_notif_count;

static void measurement_cb(struct bt_cp_client *cp_c, const struct bt_cp_client_measurement *meas,
			    int err)
{
	if (err) {
		LOG_WRN("Cycling Power Measurement parse error: %d", err);
		return;
	}
	m_cps_notif_count++;
	m_last_power_w = meas->inst_power;
	LOG_INF("Power measurement: %d W", meas->inst_power);
}

static void vector_cb(struct bt_cp_client *cp_c, const struct bt_cp_client_vector *vec, int err)
{
	if (err) {
		LOG_WRN("Cycling Power Vector parse error: %d", err);
		return;
	}
	LOG_INF("Power vector: crank_rev=%u array_size=%u", vec->cumul_crank_rev, vec->array_size);
}

static void cps_discovery_completed_cb(struct bt_gatt_dm *dm, void *ctx)
{
	int err;

	LOG_INF("Cycling Power Service discovered");
	m_cps_discovered = true;

	err = bt_cp_client_handles_assign(dm, &cp_c);
	if (err) {
		LOG_ERR("Could not assign CP client handles (err %d)", err);
		bt_gatt_dm_data_release(dm);
		return;
	}

	err = bt_cp_client_measurement_subscribe(&cp_c, measurement_cb);
	if (err) {
		LOG_ERR("Could not subscribe to Power Measurement (err %d)", err);
	}

	if (bt_cp_client_has_vector(&cp_c)) {
		err = bt_cp_client_vector_subscribe(&cp_c, vector_cb);
		if (err) {
			LOG_ERR("Could not subscribe to Power Vector (err %d)", err);
		}
	}

	bt_gatt_dm_data_release(dm);
}

static void cps_discovery_not_found_cb(struct bt_conn *conn, void *ctx)
{
	LOG_WRN("Cycling Power Service not found on peer");
}

static void cps_discovery_error_cb(struct bt_conn *conn, int err, void *ctx)
{
	LOG_ERR("Discovery failed (err %d)", err);
}

static const struct bt_gatt_dm_cb cps_discovery_cb = {
	.completed = cps_discovery_completed_cb,
	.service_not_found = cps_discovery_not_found_cb,
	.error_found = cps_discovery_error_cb,
};

static void hrs_measurement_cb(struct bt_hrs_client *hrs_c, const struct bt_hrs_client_measurement *meas,
				int err)
{
	if (err) {
		LOG_WRN("Heart Rate Measurement parse error: %d", err);
		return;
	}
	m_hrs_notif_count++;
	LOG_INF("Heart rate: %u bpm%s", meas->hr_value,
		meas->flags.rr_intervals_present ? " (RR intervals present)" : "");
}

static void hrs_discovery_completed_cb(struct bt_gatt_dm *dm, void *ctx)
{
	int err;

	LOG_INF("Heart Rate Service discovered");
	m_hrs_discovered = true;

	err = bt_hrs_client_handles_assign(dm, &hrs_c);
	if (err) {
		LOG_ERR("Could not assign HRS client handles (err %d)", err);
		bt_gatt_dm_data_release(dm);
		return;
	}

	err = bt_hrs_client_measurement_subscribe(&hrs_c, hrs_measurement_cb);
	if (err) {
		LOG_ERR("Could not subscribe to HR Measurement (err %d)", err);
	}

	bt_gatt_dm_data_release(dm);
}

static void hrs_discovery_not_found_cb(struct bt_conn *conn, void *ctx)
{
	LOG_WRN("Heart Rate Service not found on peer");
}

static void hrs_discovery_error_cb(struct bt_conn *conn, int err, void *ctx)
{
	LOG_ERR("Discovery failed (err %d)", err);
}

static const struct bt_gatt_dm_cb hrs_discovery_cb = {
	.completed = hrs_discovery_completed_cb,
	.service_not_found = hrs_discovery_not_found_cb,
	.error_found = hrs_discovery_error_cb,
};

static void scan_start(void)
{
	int err = bt_scan_start(BT_SCAN_TYPE_SCAN_PASSIVE);

	if (err) {
		LOG_ERR("Scanning failed to start (err %d)", err);
	} else {
		LOG_INF("Scanning for Cycling Power / Heart Rate Service peripherals...");
	}
}

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (conn_err) {
		LOG_WRN("Failed to connect to %s (0x%02x %s)", addr, conn_err,
			bt_hci_err_to_str(conn_err));
		if (conn == default_conn) {
			bt_conn_unref(default_conn);
			default_conn = NULL;
			scan_start();
		}
		return;
	}

	LOG_INF("Connected: %s", addr);

	/* smp_demo.c added a BLE peripheral role (MCUmgr/SMP DFU transport)
	 * alongside this file's own central role -- an inbound connection
	 * from an SMP host client fires this same global BT_CONN_CB_DEFINE
	 * callback, and without this check would wrongly run the
	 * central-role GATT discovery-manager logic below (meant only for
	 * OUR OWN outbound connections to CP/HRS peripherals) against a
	 * connection where this device is actually the GATT server. */
	struct bt_conn_info info;

	bt_conn_get_info(conn, &info);
	if (info.role != BT_CONN_ROLE_CENTRAL) {
		return;
	}

	/* matched_service was recorded by scan_filter_match() when this
	 * device's advertisement matched one of the two registered UUID
	 * filters -- decides which single service to discover on this
	 * connection (see the file header comment for why only one). */
	int err;

	if (matched_service == BLE_MATCHED_SERVICE_HRS) {
		err = bt_gatt_dm_start(conn, BT_UUID_HRS, &hrs_discovery_cb, NULL);
	} else {
		err = bt_gatt_dm_start(conn, BT_UUID_CPS, &cps_discovery_cb, NULL);
	}

	if (err) {
		LOG_ERR("Could not start discovery (err %d)", err);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("Disconnected (reason 0x%02x %s)", reason, bt_hci_err_to_str(reason));

	if (conn == default_conn) {
		bt_conn_unref(default_conn);
		default_conn = NULL;
		scan_start();
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

static void scan_connecting(struct bt_scan_device_info *device_info, struct bt_conn *conn)
{
	default_conn = bt_conn_ref(conn);
}

static void scan_connecting_error(struct bt_scan_device_info *device_info)
{
	LOG_ERR("Connecting to scanned device failed");
}

static void scan_filter_match(struct bt_scan_device_info *device_info,
			       struct bt_scan_filter_match *filter_match, bool connectable)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(device_info->recv_info->addr, addr, sizeof(addr));

	matched_service = BLE_MATCHED_SERVICE_NONE;
	if (filter_match->uuid.match && filter_match->uuid.count > 0) {
		if (!bt_uuid_cmp(filter_match->uuid.uuid[0], BT_UUID_HRS)) {
			matched_service = BLE_MATCHED_SERVICE_HRS;
		} else if (!bt_uuid_cmp(filter_match->uuid.uuid[0], BT_UUID_CPS)) {
			matched_service = BLE_MATCHED_SERVICE_CPS;
		}
	}

	LOG_INF("%s peripheral found: %s (connectable: %d)",
		matched_service == BLE_MATCHED_SERVICE_HRS ? "Heart Rate Service"
							     : "Cycling Power Service",
		addr, connectable);
}

BT_SCAN_CB_INIT(scan_cb, scan_filter_match, NULL, scan_connecting_error, scan_connecting);

void ble_demo_start(void)
{
	int err;

	/* 2026-09-11: the real "GPS Ally" app's own BluetoothLeService (see
	 * BluetoothLeService.txt at the repo root, decompiled from the real
	 * app) doesn't only scan by service UUID -- every scan result also
	 * has to pass isLezyneDevice(), which is a hardcoded check on the
	 * peer's BLE address, not anything advertised:
	 *   return strArrSplit[4].equalsIgnoreCase("37") &&
	 *          strArrSplit[5].equalsIgnoreCase("B4");
	 * i.e. the address's last two octets (as Android's colon-separated
	 * getAddress() splits them) must be 37:B4 -- Lezyne's own real units
	 * apparently all share that address suffix. Without this, GPS Ally's
	 * own ScanFilter (setServiceUuid(), matched correctly by this port's
	 * lezyne_ble.c) still passes the device to onScanResult(), but
	 * isLezyneDevice() then silently rejects it and GPS Ally shows
	 * nothing -- exactly the "app can't see the device" symptom seen
	 * with this board's normal (FICR-derived) random static address, an
	 * essentially-zero chance of a coincidental 37:B4 suffix.
	 *
	 * Forcing that suffix here (kept before bt_enable(), matching the
	 * documented bt_id_create() pattern in nrf/samples/bluetooth/
	 * rssi_power_control/peripheral/src/main.c) makes this identity 0's
	 * only address for as long as the board stays on this firmware --
	 * the whole device (Lezyne peripheral advertising in lezyne_ble.c
	 * AND this file's own central-role scanning/connecting) shares one
	 * BT stack and one default identity, so there's nowhere more
	 * Lezyne-specific to put this. The other four octets are otherwise
	 * arbitrary (kept close to this board's own previous FICR-derived
	 * address purely for continuity); the top two bits of the first
	 * octet must stay "11" for a legal static random address (0xD0 =
	 * 0b110100_00 satisfies that). */
	bt_addr_le_t lezyne_addr;

	err = bt_addr_le_from_str("D0:8F:6A:4A:37:B4", "random", &lezyne_addr);
	if (err) {
		LOG_ERR("bt_addr_le_from_str() failed (err %d)", err);
	} else {
		err = bt_id_create(&lezyne_addr, NULL);
		if (err < 0) {
			LOG_ERR("bt_id_create() failed (err %d)", err);
		}
	}

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("bt_enable() failed (err %d)", err);
		return;
	}
	LOG_INF("Bluetooth initialized");

	err = bt_cp_client_init(&cp_c);
	if (err) {
		LOG_ERR("bt_cp_client_init() failed (err %d)", err);
		return;
	}

	err = bt_hrs_client_init(&hrs_c);
	if (err) {
		LOG_ERR("bt_hrs_client_init() failed (err %d)", err);
		return;
	}

	struct bt_scan_init_param scan_init = {
		.scan_param = NULL,
		.conn_param = BT_LE_CONN_PARAM_DEFAULT,
		.connect_if_match = 1,
	};

	bt_scan_init(&scan_init);
	bt_scan_cb_register(&scan_cb);

	err = bt_scan_filter_add(BT_SCAN_FILTER_TYPE_UUID, BT_UUID_CPS);
	if (err) {
		LOG_ERR("Could not add CPS scan filter (err %d)", err);
	}

	err = bt_scan_filter_add(BT_SCAN_FILTER_TYPE_UUID, BT_UUID_HRS);
	if (err) {
		LOG_ERR("Could not add HRS scan filter (err %d)", err);
	}

	err = bt_scan_filter_enable(BT_SCAN_UUID_FILTER, false);
	if (err) {
		LOG_ERR("Could not enable scan filter (err %d)", err);
	}

	scan_start();
}

void ble_demo_log_status(void)
{
	LOG_INF("ble_demo: connected=%d matched_service=%d", default_conn != NULL,
		matched_service);
	LOG_INF("ble_demo: CPS discovered=%d notif_count=%u last_power=%d W", m_cps_discovered,
		m_cps_notif_count, m_last_power_w);
	LOG_INF("ble_demo: HRS discovered=%d notif_count=%u", m_hrs_discovered, m_hrs_notif_count);
}
