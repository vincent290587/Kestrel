/*
 * Phase 5 ANT+ feasibility test: does sdk-ant 2.1.1 (officially matched to
 * sdk-nrf v3.2.4) actually build and run against our pinned v3.3.4? This is
 * deliberately just ant_init() + a single wildcard RX channel -- modeled on
 * ant/samples/ant_broadcast_rx -- not yet any of stravaV10's real ANT+
 * profiles (FE-C/HRM/BSC). Real hardware: DK has the required 32.768kHz
 * crystal, but no ANT+ sensor is nearby to actually receive from -- same
 * "clean init, no peer to validate against" tier as everything else that
 * needed real hardware nobody's plugged in yet.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ant_error.h"
#include "ant_interface.h"
#include "ant_parameters.h"
#include "ant_host_init.h"
#include "ant_channel_config.h"

LOG_MODULE_REGISTER(stravav11_ant, LOG_LEVEL_INF);

#define TEST_CHANNEL_NUM 0
#define TEST_NETWORK_NUM 0

static void ant_evt_handler(ant_evt_t *evt)
{
	if (evt->channel != TEST_CHANNEL_NUM) {
		return;
	}

	switch (evt->event) {
	case EVENT_RX:
		LOG_INF("EVENT_RX on channel %u", evt->channel);
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

int main(void)
{
	ant_err_t err;

	err = ant_init();
	if (err) {
		LOG_ERR("ant_init() failed: 0x%X", err);
		return 0;
	}
	LOG_INF("ANT stack initialized, version %s", ANT_VERSION_STRING);

	err = ant_cb_register(&ant_evt_handler);
	if (err) {
		LOG_ERR("ant_cb_register() failed: 0x%X", err);
		return 0;
	}

	/* Wildcard slave channel: device number/type/transmission type 0
	 * means "match any broadcaster", matching the standard ANT+
	 * discovery pattern. RF freq 66 (2466 MHz) and channel period 8192
	 * (4 Hz) are the common ANT+ defaults, same as the sdk-ant
	 * ant_broadcast_rx sample uses.
	 */
	ant_channel_config_t config = {
		.channel_number = TEST_CHANNEL_NUM,
		.channel_type = CHANNEL_TYPE_SLAVE,
		.ext_assign = 0x00,
		.rf_freq = 66,
		.transmission_type = 0,
		.device_type = 0,
		.device_number = 0,
		.channel_period = 8192,
		.network_number = TEST_NETWORK_NUM,
	};

	err = ant_channel_init(&config);
	if (err) {
		LOG_ERR("ant_channel_init() failed: 0x%X", err);
		return 0;
	}

	err = ant_channel_open(TEST_CHANNEL_NUM);
	if (err) {
		LOG_ERR("ant_channel_open() failed: 0x%X", err);
		return 0;
	}

	LOG_INF("ANT wildcard RX channel open, listening...");

	return 0;
}
