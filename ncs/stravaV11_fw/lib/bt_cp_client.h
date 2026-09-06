/*
 * Bluetooth LE GATT client for the Cycling Power Service (0x1818), ported
 * from stravaV10's libraries/ble_services/ble_cp_c (an nRF5 SDK ble_db_discovery
 * client) to Zephyr's Bluetooth host API, following the structure of NCS's
 * own bt_hrs_client (nrf/subsys/bluetooth/services/hrs_client.c) -- the
 * closest upstream analog, since Zephyr/NCS has no built-in Cycling Power
 * Service client to reuse.
 *
 * Parses the two characteristics stravaV10 actually used: Cycling Power
 * Measurement (0x2A63) and Cycling Power Vector (0x2A64), per the Bluetooth
 * SIG GATT Specification Supplement -- the wire format itself is unchanged
 * from stravaV10's power_measure_decode()/power_vector_decode(), only the
 * BLE plumbing (subscription/discovery) is new.
 */

#ifndef BT_CP_CLIENT_H_
#define BT_CP_CLIENT_H_

#include <stdint.h>
#include <zephyr/types.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <bluetooth/gatt_dm.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BT_UUID_CPS_VAL            0x1818
#define BT_UUID_CPS_MEASUREMENT_VAL 0x2A63
#define BT_UUID_CPS_VECTOR_VAL      0x2A64

#define BT_UUID_CPS            BT_UUID_DECLARE_16(BT_UUID_CPS_VAL)
#define BT_UUID_CPS_MEASUREMENT BT_UUID_DECLARE_16(BT_UUID_CPS_MEASUREMENT_VAL)
#define BT_UUID_CPS_VECTOR       BT_UUID_DECLARE_16(BT_UUID_CPS_VECTOR_VAL)

/* Cycling Power Vector: flags octet (Bluetooth GSS 3.66). */
struct bt_cp_client_vector_flags {
	uint8_t crank_rev_data: 1;
	uint8_t first_angle_data: 1;
	uint8_t f_mag_array: 1;
	uint8_t inst_torque_array: 1;
	uint8_t inst_direction: 2;
	uint8_t reserved: 2;
};

/* Cycling Power Vector characteristic value, accumulated across notifications
 * (the force/torque array can span multiple packets on the wire, same as
 * stravaV10's power_vector_decode()).
 */
struct bt_cp_client_vector {
	union {
		struct bt_cp_client_vector_flags flags;
		uint8_t raw_flags;
	};
	uint16_t cumul_crank_rev;
	uint16_t last_crank_evt; /* seconds, 1/1024 resolution */
	uint16_t first_crank_angle; /* degrees */
	union {
		int16_t inst_force_mag_array[64]; /* newtons */
		int16_t inst_torque_mag_array[64]; /* newton-metres, 1/32 resolution */
	};
	uint16_t array_size;
};

/* Cycling Power Measurement: flags field (Bluetooth GSS 3.65). */
struct bt_cp_client_meas_flags {
	uint16_t pedal_power_balance: 1;
	uint16_t pedal_power_balance_ref: 1;
	uint16_t acc_torque: 1;
	uint16_t acc_torque_source: 1;
	uint16_t wheel_rev: 1;
	uint16_t crank_rev: 1;
	uint16_t extr_force_mag: 1;
	uint16_t extr_torque_mag: 1;
	uint16_t extr_angles: 1;
	uint16_t top_dead_spot_angle: 1;
	uint16_t bottom_dead_spot_angle: 1;
	uint16_t acc_energy: 1;
	uint16_t offset_comp_ind: 1;
	uint16_t reserved: 3;
};

struct bt_cp_client_measurement {
	union {
		struct bt_cp_client_meas_flags flags;
		uint16_t raw_flags;
	};
	int16_t inst_power; /* watts */
	uint8_t pedal_power_balance;
	uint16_t acc_torque;
	uint32_t cumul_wheel_rev;
	uint16_t last_wheel_evt; /* seconds, 1/2048 resolution */
	uint16_t cumul_crank_rev;
	uint16_t last_crank_evt; /* seconds, 1/1024 resolution */
};

struct bt_cp_client;

typedef void (*bt_cp_client_vector_cb)(struct bt_cp_client *cp_c,
					const struct bt_cp_client_vector *vec, int err);
typedef void (*bt_cp_client_measurement_cb)(struct bt_cp_client *cp_c,
					     const struct bt_cp_client_measurement *meas,
					     int err);

struct bt_cp_client_char {
	uint16_t handle;
	uint16_t ccc_handle;
	struct bt_gatt_subscribe_params notify_params;
};

struct bt_cp_client {
	struct bt_conn *conn;
	atomic_t state;

	struct bt_cp_client_char measurement_char;
	struct bt_cp_client_char vector_char;

	bt_cp_client_measurement_cb measurement_cb;
	bt_cp_client_vector_cb vector_cb;

	/* Vector value accumulates across notifications (force/torque array
	 * can be split across packets), same as stravaV10.
	 */
	struct bt_cp_client_vector vector;
};

/** Zero-initialize a Cycling Power client instance. */
int bt_cp_client_init(struct bt_cp_client *cp_c);

/** Assign GATT handles found by bt_gatt_dm_start(conn, BT_UUID_CPS, ...). */
int bt_cp_client_handles_assign(struct bt_gatt_dm *dm, struct bt_cp_client *cp_c);

/** Subscribe to the Cycling Power Measurement characteristic's notifications. */
int bt_cp_client_measurement_subscribe(struct bt_cp_client *cp_c,
					bt_cp_client_measurement_cb cb);

/** Subscribe to the Cycling Power Vector characteristic's notifications. */
int bt_cp_client_vector_subscribe(struct bt_cp_client *cp_c, bt_cp_client_vector_cb cb);

static inline bool bt_cp_client_has_vector(const struct bt_cp_client *cp_c)
{
	return cp_c->vector_char.handle != 0;
}

#ifdef __cplusplus
}
#endif

#endif /* BT_CP_CLIENT_H_ */
