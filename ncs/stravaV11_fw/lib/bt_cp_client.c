#include "bt_cp_client.h"

#include <string.h>
#include <errno.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(bt_cp_client, LOG_LEVEL_INF);

#define CP_MEASUREMENT_NOTIFY_ENABLED BIT(0)
#define CP_VECTOR_NOTIFY_ENABLED      BIT(1)

static void cp_client_reinit(struct bt_cp_client *cp_c)
{
	cp_c->measurement_char.handle = 0;
	cp_c->measurement_char.ccc_handle = 0;
	cp_c->vector_char.handle = 0;
	cp_c->vector_char.ccc_handle = 0;
	cp_c->conn = NULL;
	cp_c->state = ATOMIC_INIT(0);
	memset(&cp_c->vector, 0, sizeof(cp_c->vector));
}

/* Ported from stravaV10's power_measure_decode() (ble_cp_c.c) -- wire format
 * per the Bluetooth SIG GATT Specification Supplement, Cycling Power
 * Measurement (0x2A63). Only present fields (per the flags bitfield) are on
 * the wire; absent ones keep whatever was already in *meas.
 */
static int cp_measurement_parse(struct bt_cp_client_measurement *meas, const uint8_t *data,
				 uint16_t length)
{
	uint16_t index = 0;

	if (length < 4) {
		return -EMSGSIZE;
	}

	meas->raw_flags = sys_get_le16(&data[index]);
	index += 2;

	meas->inst_power = (int16_t)sys_get_le16(&data[index]);
	index += 2;

	if (meas->flags.pedal_power_balance) {
		if (index + 1 > length) {
			return -EMSGSIZE;
		}
		meas->pedal_power_balance = data[index];
		index += 1;
	}

	if (meas->flags.acc_torque) {
		if (index + 2 > length) {
			return -EMSGSIZE;
		}
		meas->acc_torque = sys_get_le16(&data[index]);
		index += 2;
	}

	if (meas->flags.wheel_rev) {
		if (index + 6 > length) {
			return -EMSGSIZE;
		}
		meas->cumul_wheel_rev = sys_get_le32(&data[index]);
		index += 4;
		meas->last_wheel_evt = sys_get_le16(&data[index]);
		index += 2;
	}

	if (meas->flags.crank_rev) {
		if (index + 4 > length) {
			return -EMSGSIZE;
		}
		meas->cumul_crank_rev = sys_get_le16(&data[index]);
		index += 2;
		meas->last_crank_evt = sys_get_le16(&data[index]);
		index += 2;
	}

	return 0;
}

/* Ported from stravaV10's power_vector_decode() (ble_cp_c.c) -- Cycling
 * Power Vector (0x2A64). The force/torque array can be split across
 * multiple notifications, so *vec accumulates between calls exactly like
 * the original (array_size only resets when a new crank-rev-data packet
 * starts a fresh vector).
 */
static int cp_vector_parse(struct bt_cp_client_vector *vec, const uint8_t *data, uint16_t length)
{
	uint16_t i = vec->array_size;
	uint16_t index = 0;

	if (length < 1) {
		return -EMSGSIZE;
	}

	vec->raw_flags = data[index++];

	if (vec->flags.crank_rev_data) {
		if (index + 4 > length) {
			return -EMSGSIZE;
		}

		/* A new crank-rev packet starts a fresh vector accumulation. */
		vec->array_size = i = 0;

		vec->cumul_crank_rev = sys_get_le16(&data[index]);
		index += 2;
		vec->last_crank_evt = sys_get_le16(&data[index]);
		index += 2;
	}

	if (vec->flags.first_angle_data) {
		if (index + 2 > length) {
			return -EMSGSIZE;
		}
		vec->first_crank_angle = sys_get_le16(&data[index]);
		index += 2;
	}

	while (vec->flags.inst_torque_array && index + 1 < length &&
	       i < ARRAY_SIZE(vec->inst_torque_mag_array)) {
		vec->inst_torque_mag_array[i++] = (int16_t)sys_get_le16(&data[index]);
		index += 2;
	}

	while (vec->flags.f_mag_array && index + 1 < length &&
	       i < ARRAY_SIZE(vec->inst_force_mag_array)) {
		vec->inst_force_mag_array[i++] = (int16_t)sys_get_le16(&data[index]);
		index += 2;
	}

	vec->array_size = i;

	return 0;
}

static uint8_t on_measurement_notify(struct bt_conn *conn, struct bt_gatt_subscribe_params *params,
				      const void *data, uint16_t length)
{
	struct bt_cp_client *cp_c =
		CONTAINER_OF(params, struct bt_cp_client, measurement_char.notify_params);
	struct bt_cp_client_measurement meas = { 0 };
	int err;

	if (!data) {
		atomic_clear_bit(&cp_c->state, CP_MEASUREMENT_NOTIFY_ENABLED);
		LOG_DBG("[UNSUBSCRIBE] Cycling Power Measurement");
		return BT_GATT_ITER_STOP;
	}

	err = cp_measurement_parse(&meas, data, length);
	if (cp_c->measurement_cb) {
		cp_c->measurement_cb(cp_c, &meas, err);
	}

	return BT_GATT_ITER_CONTINUE;
}

static uint8_t on_vector_notify(struct bt_conn *conn, struct bt_gatt_subscribe_params *params,
				 const void *data, uint16_t length)
{
	struct bt_cp_client *cp_c =
		CONTAINER_OF(params, struct bt_cp_client, vector_char.notify_params);
	int err;

	if (!data) {
		atomic_clear_bit(&cp_c->state, CP_VECTOR_NOTIFY_ENABLED);
		LOG_DBG("[UNSUBSCRIBE] Cycling Power Vector");
		return BT_GATT_ITER_STOP;
	}

	err = cp_vector_parse(&cp_c->vector, data, length);
	if (cp_c->vector_cb) {
		cp_c->vector_cb(cp_c, &cp_c->vector, err);
	}

	return BT_GATT_ITER_CONTINUE;
}

static int cp_client_subscribe(struct bt_cp_client *cp_c, struct bt_cp_client_char *chr,
				bt_gatt_notify_func_t notify_fn, uint32_t enabled_bit)
{
	struct bt_gatt_subscribe_params *params = &chr->notify_params;
	int err;

	if (chr->handle == 0) {
		return -ENOTSUP;
	}

	if (atomic_test_and_set_bit(&cp_c->state, enabled_bit)) {
		return -EALREADY;
	}

	params->ccc_handle = chr->ccc_handle;
	params->value_handle = chr->handle;
	params->value = BT_GATT_CCC_NOTIFY;
	params->notify = notify_fn;
	atomic_set_bit(params->flags, BT_GATT_SUBSCRIBE_FLAG_VOLATILE);

	err = bt_gatt_subscribe(cp_c->conn, params);
	if (err) {
		atomic_clear_bit(&cp_c->state, enabled_bit);
		LOG_ERR("Subscribe failed (err %d)", err);
	}

	return err;
}

int bt_cp_client_measurement_subscribe(struct bt_cp_client *cp_c, bt_cp_client_measurement_cb cb)
{
	if (!cp_c || !cb) {
		return -EINVAL;
	}

	cp_c->measurement_cb = cb;
	return cp_client_subscribe(cp_c, &cp_c->measurement_char, on_measurement_notify,
				    CP_MEASUREMENT_NOTIFY_ENABLED);
}

int bt_cp_client_vector_subscribe(struct bt_cp_client *cp_c, bt_cp_client_vector_cb cb)
{
	if (!cp_c || !cb) {
		return -EINVAL;
	}

	cp_c->vector_cb = cb;
	return cp_client_subscribe(cp_c, &cp_c->vector_char, on_vector_notify,
				    CP_VECTOR_NOTIFY_ENABLED);
}

int bt_cp_client_handles_assign(struct bt_gatt_dm *dm, struct bt_cp_client *cp_c)
{
	const struct bt_gatt_dm_attr *service_attr = bt_gatt_dm_service_get(dm);
	const struct bt_gatt_service_val *service = bt_gatt_dm_attr_service_val(service_attr);
	const struct bt_gatt_dm_attr *chrc;
	const struct bt_gatt_dm_attr *desc;

	if (!dm || !cp_c) {
		return -EINVAL;
	}

	if (bt_uuid_cmp(service->uuid, BT_UUID_CPS)) {
		return -ENOTSUP;
	}

	cp_client_reinit(cp_c);

	chrc = bt_gatt_dm_char_by_uuid(dm, BT_UUID_CPS_MEASUREMENT);
	if (!chrc) {
		LOG_ERR("No Cycling Power Measurement characteristic found");
		return -EINVAL;
	}
	desc = bt_gatt_dm_desc_by_uuid(dm, chrc, BT_UUID_CPS_MEASUREMENT);
	if (!desc) {
		return -EINVAL;
	}
	cp_c->measurement_char.handle = desc->handle;

	desc = bt_gatt_dm_desc_by_uuid(dm, chrc, BT_UUID_GATT_CCC);
	if (!desc) {
		LOG_ERR("No Cycling Power Measurement CCC descriptor found");
		return -EINVAL;
	}
	cp_c->measurement_char.ccc_handle = desc->handle;

	/* Cycling Power Vector is optional -- not every sensor implements it. */
	chrc = bt_gatt_dm_char_by_uuid(dm, BT_UUID_CPS_VECTOR);
	if (chrc) {
		desc = bt_gatt_dm_desc_by_uuid(dm, chrc, BT_UUID_CPS_VECTOR);
		if (desc) {
			cp_c->vector_char.handle = desc->handle;

			desc = bt_gatt_dm_desc_by_uuid(dm, chrc, BT_UUID_GATT_CCC);
			if (desc) {
				cp_c->vector_char.ccc_handle = desc->handle;
			}
		}
	} else {
		LOG_DBG("No Cycling Power Vector characteristic found");
	}

	cp_c->conn = bt_gatt_dm_conn_get(dm);

	return 0;
}

int bt_cp_client_init(struct bt_cp_client *cp_c)
{
	if (!cp_c) {
		return -EINVAL;
	}

	memset(cp_c, 0, sizeof(*cp_c));

	return 0;
}
