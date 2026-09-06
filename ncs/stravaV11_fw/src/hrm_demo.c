/*
 * ANT+ HRM (heart rate monitor) display profile, ported from stravaV10's
 * rf/hrm.c onto sdk-ant's ready-made ant_hrm library (ncs/ant/include/
 * ant_profiles/hrm/ant_hrm.h) -- field/macro names (page_0.computed_heart_rate,
 * page_4.prev_beat, beat_count, ...) match stravaV10's own usage exactly,
 * both being derived from the same historical Nordic ant_profiles codebase.
 * Integration pattern (ant_plus_key_set(), event handler registration) is
 * from ant/samples/ant_plus/ant_hrm/hrm_rx, sdk-ant's own reference sample.
 *
 * Device number/channel number match stravaV10's rf/ant_device_manager.h
 * (HRM_DEVICE_NUMBER=17334, HRM_CHANNEL_NUMBER=2) so this pairs with the
 * same physical strap stravaV10 was configured for. Channel 0 stays
 * reserved for ant_demo.c's wildcard channel; BSC uses channel 1.
 *
 * Not ported: stravaV10's optional USE_ANT_SEARCH block (custom low-priority
 * search tuning) -- sdk-ant's channel defaults are used as-is. Not adopted:
 * the hrm_rx sample's ant_state_indicator/dk_buttons_and_leds LED status
 * helpers, to stay consistent with this port's existing plain-LOG_INF demo
 * style (ant_demo.c, ble_demo.c, ...).
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ant_error.h"
#include "ant_interface.h"
#include "ant_parameters.h"
#include "ant_channel_config.h"
#include "ant_key_manager.h"
#include "ant_profiles/hrm/ant_hrm.h"

#include "hrm_demo.h"

LOG_MODULE_REGISTER(hrm_demo, LOG_LEVEL_INF);

#define HRM_CHANNEL_NUMBER      2
#define HRM_NETWORK_NUMBER      0 /* ANTPLUS_NETWORK_NUMBER in stravaV10 */
/* Wildcard (0), not stravaV10's hardcoded HRM_DEVICE_NUMBER (17334U, from
 * rf/ant_device_manager.h -- presumably whatever specific strap the
 * original developer had paired). This port has no real pairing/discovery
 * UI yet (ant_device_manager.cpp isn't ported -- see CLAUDE.md Phase 5),
 * so a fixed device number would never match a real strap unless it
 * happened to share that exact ID. ant_evt_hrm() already logs "HRM paired
 * with device %u" via ant_channel_id_get() once a wildcard search finds
 * one -- that's the number a future real pairing UI would persist. */
#define HRM_DEVICE_NUMBER       0
#define WILDCARD_TRANSMISSION_TYPE 0

HRM_DISP_CHANNEL_CONFIG_DEF(m_hrm, HRM_CHANNEL_NUMBER, WILDCARD_TRANSMISSION_TYPE,
			     HRM_DEVICE_NUMBER, HRM_NETWORK_NUMBER, HRM_MSG_PERIOD_4Hz);

static ant_hrm_profile_t m_hrm;
static uint8_t m_reconn_counts;
static uint8_t m_paired;

/* Ported values, in place of stravaV10's g_structs.h hrm_info -- Model.cpp
 * isn't wired into stravaV11_fw yet, so this demo just logs. */
static uint8_t m_bpm;
static uint16_t m_rr_ms;

static void hrm_connect(void)
{
	int err = ant_hrm_disp_open(&m_hrm);

	if (err) {
		LOG_ERR("ant_hrm_disp_open() failed: %d", err);
		return;
	}
	LOG_INF("HRM search restarted");
}

static void ant_hrm_evt_handler(ant_hrm_profile_t *p_profile, ant_hrm_evt_t event)
{
	static uint32_t s_previous_beat_count;
	uint16_t beat_time = p_profile->page_0.beat_time;
	uint32_t beat_count = p_profile->page_0.beat_count;

	m_bpm = p_profile->page_0.computed_heart_rate;

	switch (event) {
	case ANT_HRM_PAGE_0_UPDATED:
		m_reconn_counts = 0;
		LOG_INF("HRM bpm=%u", m_bpm);
		break;

	case ANT_HRM_PAGE_4_UPDATED:
		/* Ensure there's exactly one beat between time intervals
		 * before trusting the delta (matches stravaV10 rf/hrm.c). */
		if ((beat_count - s_previous_beat_count) == 1) {
			uint16_t prev_beat = p_profile->page_4.prev_beat;
			uint16_t rr_interval = beat_time - prev_beat;

			m_rr_ms = (uint16_t)(rr_interval * 1000.f / 1024.f);
			LOG_INF("HRM rr=%u ms (uptime %lld ms)", m_rr_ms, k_uptime_get());
		}
		s_previous_beat_count = beat_count;
		break;

	default:
		break;
	}
}

static void ant_evt_hrm(ant_evt_t *p_ant_evt)
{
	if (p_ant_evt->channel != HRM_CHANNEL_NUMBER) {
		return;
	}

	switch (p_ant_evt->event) {
	case EVENT_RX:
		if (!m_paired) {
			uint16_t device_number = 0;
			uint8_t device_type = 0, transmit_type = 0;

			ant_channel_id_get(HRM_CHANNEL_NUMBER, &device_number, &device_type,
					    &transmit_type);
			if (device_number) {
				m_paired = 1;
				LOG_INF("HRM paired with device %u", device_number);
			}
		}
		ant_hrm_disp_evt_handler(p_ant_evt, &m_hrm);
		break;
	case EVENT_RX_FAIL_GO_TO_SEARCH:
		LOG_INF("HRM search restarted (RX fail)");
		break;
	case EVENT_RX_SEARCH_TIMEOUT:
		LOG_INF("HRM search timeout");
		break;
	case EVENT_CHANNEL_CLOSED:
		LOG_INF("HRM channel closed");
		m_paired = 0;
		if (m_reconn_counts < 5) {
			m_reconn_counts++;
			hrm_connect();
		}
		break;
	default:
		break;
	}
}

int hrm_demo_start(void)
{
	int err = ant_plus_key_set(HRM_NETWORK_NUMBER);

	if (err) {
		LOG_ERR("ant_plus_key_set() failed: %d", err);
		return err;
	}

	err = ant_hrm_disp_init(&m_hrm, HRM_DISP_CHANNEL_CONFIG(m_hrm), ant_hrm_evt_handler);
	if (err) {
		LOG_ERR("ant_hrm_disp_init() failed: %d", err);
		return err;
	}

	err = ant_cb_register(&ant_evt_hrm);
	if (err) {
		LOG_ERR("ant_cb_register() failed: %d", err);
		return err;
	}

	err = ant_hrm_disp_open(&m_hrm);
	if (err) {
		LOG_ERR("ant_hrm_disp_open() failed: %d", err);
		return err;
	}

	LOG_INF("ANT+ HRM channel %u open, searching for device %u...", HRM_CHANNEL_NUMBER,
		HRM_DEVICE_NUMBER);

	return 0;
}

uint8_t hrm_demo_get_bpm(void)
{
	return m_bpm;
}

uint16_t hrm_demo_get_rr_ms(void)
{
	return m_rr_ms;
}

bool hrm_demo_is_paired(void)
{
	return m_paired != 0;
}
