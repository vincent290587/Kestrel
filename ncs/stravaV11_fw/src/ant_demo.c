/*
 * Phase 5 ANT+ bring-up, now combined with the BLE central code in
 * ble_demo.c -- same wildcard RX channel validated standalone in
 * stravaV11_ant, moved here to prove CONFIG_ANT + CONFIG_BT coexist on one
 * build (the documented multiprotocol path). Call ant_demo_start() before
 * ble_demo_start(): both sit on MPSL, and this matches the init order used
 * by sdk-ant's own combined reference sample (ant/samples/ble_ant_app_hrm),
 * which brings ANT up before calling bt_enable().
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ant_error.h"
#include "ant_interface.h"
#include "ant_parameters.h"
#include "ant_host_init.h"
#include "ant_channel_config.h"

#include "ant_demo.h"

LOG_MODULE_REGISTER(ant_demo, LOG_LEVEL_INF);

#define ANT_TEST_CHANNEL_NUM 0
#define ANT_TEST_NETWORK_NUM 0

static void ant_evt_handler(ant_evt_t *evt)
{
	if (evt->channel != ANT_TEST_CHANNEL_NUM) {
		return;
	}

	switch (evt->event) {
	case EVENT_RX:
		LOG_INF("EVENT_RX on ANT channel %u", evt->channel);
		break;
	case EVENT_RX_SEARCH_TIMEOUT:
		LOG_INF("EVENT_RX_SEARCH_TIMEOUT");
		break;
	case EVENT_RX_FAIL:
		LOG_INF("EVENT_RX_FAIL");
		break;
	default:
		LOG_INF("ANT event 0x%X", evt->event);
		break;
	}
}

int ant_demo_start(void)
{
	ant_err_t err = ant_init();

	if (err) {
		LOG_ERR("ant_init() failed: 0x%X", err);
		return err;
	}
	LOG_INF("ANT stack initialized, version %s", ANT_VERSION_STRING);

	err = ant_cb_register(&ant_evt_handler);
	if (err) {
		LOG_ERR("ant_cb_register() failed: 0x%X", err);
		return err;
	}

	/* Wildcard slave channel, same defaults as ant/samples/ant_broadcast_rx
	 * (RF freq 66 / 2466 MHz, period 8192 / 4 Hz) -- listens for any
	 * ANT+ broadcaster. */
	ant_channel_config_t config = {
		.channel_number = ANT_TEST_CHANNEL_NUM,
		.channel_type = CHANNEL_TYPE_SLAVE,
		.ext_assign = 0x00,
		.rf_freq = 66,
		.transmission_type = 0,
		.device_type = 0,
		.device_number = 0,
		.channel_period = 8192,
		.network_number = ANT_TEST_NETWORK_NUM,
	};

	err = ant_channel_init(&config);
	if (err) {
		LOG_ERR("ant_channel_init() failed: 0x%X", err);
		return err;
	}

	err = ant_channel_open(ANT_TEST_CHANNEL_NUM);
	if (err) {
		LOG_ERR("ant_channel_open() failed: 0x%X", err);
		return err;
	}

	LOG_INF("ANT wildcard RX channel open, listening...");

	return 0;
}
