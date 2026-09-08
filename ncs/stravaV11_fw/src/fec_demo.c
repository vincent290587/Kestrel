/*
 * ANT+ FE-C (Fitness Equipment Control) display profile, ported from
 * stravaV10's rf/fec.c. Unlike hrm_demo.c/bsc_demo.c (which reuse
 * sdk-ant's ready-made ant_hrm/ant_bsc libraries), sdk-ant ships no FE-C
 * profile library at all -- see ant_fec.h's top-of-file note -- so this
 * file hand-manages the channel directly against sdk-ant's raw
 * ant_channel_config_t/ant_interface.h API (the same layer ant_hrm/
 * ant_bsc themselves are built on), with ant_fec.{h,c} supplying the
 * page-format encode/decode stravaV10 already had.
 *
 * Device number/channel number follow stravaV10's rf/ant_device_manager.h
 * (FEC_CHANNEL_NUMBER=3, right after wildcard=0/BSC=1/HRM=2). Started as
 * a wildcard search (0) -- same starting point HRM used before its real
 * device number was confirmed on real hardware (CLAUDE.md Phase 11) --
 * and locked to 15568 after a real trainer actually paired under that
 * exact device number on the first hardware test, confirming stravaV10's
 * own hardcoded TACX_DEVICE_NUMBER (rf/ant_device_manager.h) was correct
 * for this specific trainer all along, not a stale/wrong leftover.
 *
 * FE-C is bidirectional (a display sends resistance/power control
 * commands back to the trainer, unlike HRM/BSC's pure RX), so this also
 * ports stravaV10's roller_manager()/roller_manager_tasks() TX path: a
 * pending control message is encoded once when fec_demo_set_control() is
 * called, then sent via ant_acknowledge_message_tx() on a periodic
 * k_work_delayable tick once the channel isn't already mid-transmission
 * (ant_pending_transmit()) -- same "self-resubmitting work item" pattern
 * as poll_demo.c (Phase 8).
 *
 * Not ported: stravaV10's page 48 (basic resistance -- never actually
 * sent, see ant_fec.h) and stravaV10's ant_device_manager.cpp discovery
 * hook (`ant_device_manager_search_add()` on EVENT_RX) -- that's the
 * separate ANT device manager porting task.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ant_error.h"
#include "ant_interface.h"
#include "ant_parameters.h"
#include "ant_channel_config.h"
#include "ant_key_manager.h"
#include "ant_host_init.h"

#include "ant_fec.h"
#include "fec_demo.h"

LOG_MODULE_REGISTER(fec_demo, LOG_LEVEL_INF);

#define FEC_CHANNEL_NUMBER 3
#define FEC_NETWORK_NUMBER 0 /* ANTPLUS_NETWORK_NUMBER in stravaV10 */
#define FEC_DEVICE_NUMBER 15568U /* confirmed on real hardware -- see this file's top-of-file note */
#define FEC_DEVICE_TYPE 0x11u
#define FEC_RF_FREQ 0x39u   /* 2457 MHz -- FEC_ANTPLUS_RF_FREQ in stravaV10 */
#define FEC_MSG_PERIOD 8192u /* 4 Hz -- FEC_MSG_PERIOD in stravaV10 */
#define FEC_CONTROL_PERIOD_MS 1500 /* FEC_CONTROL_DELAY in stravaV10 */

static const ant_channel_config_t m_fec_channel_config = {
	.channel_number = FEC_CHANNEL_NUMBER,
	.channel_type = CHANNEL_TYPE_SLAVE,
	.ext_assign = 0x00,
	.rf_freq = FEC_RF_FREQ,
	.transmission_type = 0, /* wildcard */
	.device_type = FEC_DEVICE_TYPE,
	.device_number = FEC_DEVICE_NUMBER,
	.channel_period = FEC_MSG_PERIOD,
	.network_number = FEC_NETWORK_NUMBER,
};

static uint8_t m_paired;
static uint8_t m_reconn_counts;

static uint16_t m_elapsed_time_s;
static uint8_t m_prev_elapsed_time_raw;
static bool m_time_init;

static uint16_t m_power_w;
static int64_t m_power_last_update_ms; /* 0 = never (see fec_demo_get_power_age_ms()) */

static uint8_t m_tx_buf[8]; /* page number + 7-byte payload, per ANT data message size */
static bool m_tx_pending;

static void fec_connect(void)
{
	int err = ant_channel_open(FEC_CHANNEL_NUMBER);

	if (err) {
		LOG_ERR("ant_channel_open() failed: %d", err);
		return;
	}
	LOG_INF("FEC search restarted");
}

static void handle_page16(const uint8_t *page_payload)
{
	struct ant_fec_page16 page16;

	ant_fec_page16_decode(page_payload, &page16);

	/* Same 8-bit rollover handling as stravaV10's ant_fec_evt_handler()
	 * (elapsed_time is a free-running 1/4s counter that wraps every
	 * 64s). */
	uint8_t new_time = page16.elapsed_time;

	if (!m_time_init) {
		m_time_init = true;
		m_prev_elapsed_time_raw = new_time;
	}
	uint8_t diff = new_time - m_prev_elapsed_time_raw; /* wraps correctly either way */

	m_prev_elapsed_time_raw = new_time;
	m_elapsed_time_s += diff / 4;

	LOG_INF("FEC elapsed_time=%u s speed=%u mm/s", m_elapsed_time_s, page16.speed_mm_s);
}

static void handle_page25(const uint8_t *page_payload)
{
	struct ant_fec_page25 page25;

	ant_fec_page25_decode(page_payload, &page25);
	m_power_w = page25.inst_power;
	m_power_last_update_ms = k_uptime_get();

	LOG_INF("FEC power=%u W cadence=%u status=%u", page25.inst_power, page25.inst_cad,
		page25.status);
}

static void ant_evt_fec(ant_evt_t *p_ant_evt)
{
	if (p_ant_evt->channel != FEC_CHANNEL_NUMBER) {
		return;
	}

	switch (p_ant_evt->event) {
	case EVENT_RX: {
		if (!m_paired) {
			uint16_t device_number = 0;
			uint8_t device_type = 0, transmit_type = 0;

			ant_channel_id_get(FEC_CHANNEL_NUMBER, &device_number, &device_type,
					    &transmit_type);
			if (device_number) {
				m_paired = 1;
				LOG_INF("FEC paired with device %u", device_number);
			}
		}

		const uint8_t *payload = p_ant_evt->message.ANT_MESSAGE_aucPayload;
		uint8_t page_number = payload[0];
		const uint8_t *page_payload = &payload[1];

		switch (page_number) {
		case 16:
			handle_page16(page_payload);
			break;
		case 25:
			handle_page25(page_payload);
			break;
		default:
			LOG_INF("FEC page %u (not decoded)", page_number);
			break;
		}
		break;
	}
	case EVENT_RX_FAIL_GO_TO_SEARCH:
		LOG_INF("FEC search restarted (RX fail)");
		break;
	case EVENT_RX_SEARCH_TIMEOUT:
		LOG_INF("FEC search timeout");
		break;
	case EVENT_TRANSFER_TX_FAILED:
		LOG_WRN("FEC control TX failed");
		break;
	case EVENT_CHANNEL_CLOSED:
		LOG_INF("FEC channel closed");
		m_paired = 0;
		if (m_reconn_counts < 5) {
			m_reconn_counts++;
			fec_connect();
		}
		break;
	default:
		break;
	}
}

void fec_demo_set_control(const sFecControl *control)
{
	LOG_INF("FEC control type %u", control->type);

	switch (control->type) {
	case eFecControlTargetPower:
		m_tx_buf[0] = 49; /* ANT_FEC_PAGE_49 */
		ant_fec_page49_encode(&m_tx_buf[1], control->data.power_control.target_power_w);
		m_tx_pending = true;
		break;
	case eFecControlSlope:
		m_tx_buf[0] = 51; /* ANT_FEC_PAGE_51 */
		ant_fec_page51_encode(&m_tx_buf[1], control->data.slope_control.slope_ppc,
				       control->data.slope_control.rolling_resistance);
		m_tx_pending = true;
		break;
	default:
		m_tx_pending = false;
		break;
	}
}

static void fec_control_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(fec_control_work, fec_control_work_handler);

static void fec_control_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (m_tx_pending && m_time_init) {
		uint8_t is_pending = 0;

		ant_pending_transmit(FEC_CHANNEL_NUMBER, &is_pending);
		if (!is_pending) {
			LOG_INF("FEC transmitting control page %u...", m_tx_buf[0]);
			int err = ant_acknowledge_message_tx(FEC_CHANNEL_NUMBER, sizeof(m_tx_buf),
							      m_tx_buf);

			if (err) {
				LOG_ERR("ant_acknowledge_message_tx() failed: %d", err);
			}
			m_tx_pending = false;
		} else {
			LOG_WRN("FEC transmission busy");
		}
	}

	k_work_schedule(&fec_control_work, K_MSEC(FEC_CONTROL_PERIOD_MS));
}

int fec_demo_start(void)
{
	int err = ant_plus_key_set(FEC_NETWORK_NUMBER);

	if (err) {
		LOG_ERR("ant_plus_key_set() failed: %d", err);
		return err;
	}

	err = ant_channel_init(&m_fec_channel_config);
	if (err) {
		LOG_ERR("ant_channel_init() failed: %d", err);
		return err;
	}

	err = ant_cb_register(&ant_evt_fec);
	if (err) {
		LOG_ERR("ant_cb_register() failed: %d", err);
		return err;
	}

	err = ant_channel_open(FEC_CHANNEL_NUMBER);
	if (err) {
		LOG_ERR("ant_channel_open() failed: %d", err);
		return err;
	}

	LOG_INF("ANT+ FEC channel %u open, searching for device %u...", FEC_CHANNEL_NUMBER,
		FEC_DEVICE_NUMBER);

	/* Queues a demo target-power control message so the real
	 * ant_fec_page49_encode() path runs on every boot -- this port has
	 * no UI yet to request one for real (stravaV10 itself never sends a
	 * control message until the user asks via its own UI). Note the TX
	 * gate below (m_time_init) means this queued message only actually
	 * transmits once page 16 has been received at least once, same as
	 * stravaV10's own is_fec_init gating -- with no real trainer nearby,
	 * expect this to stay queued and never sent, which is itself
	 * correct, safe behavior: this port never blind-sends acknowledged
	 * data to a channel that hasn't synced with anything yet. */
	sFecControl demo_control;

	demo_control.type = eFecControlTargetPower;
	demo_control.data.power_control.target_power_w = 100;
	fec_demo_set_control(&demo_control);

	k_work_schedule(&fec_control_work, K_MSEC(FEC_CONTROL_PERIOD_MS));

	return 0;
}

uint16_t fec_demo_get_power_w(void)
{
	return m_power_w;
}

uint16_t fec_demo_get_elapsed_time_s(void)
{
	return m_elapsed_time_s;
}

bool fec_demo_is_paired(void)
{
	return m_paired != 0;
}

uint32_t fec_demo_get_power_age_ms(void)
{
	if (m_power_last_update_ms == 0) {
		return UINT32_MAX;
	}

	return (uint32_t)(k_uptime_get() - m_power_last_update_ms);
}
