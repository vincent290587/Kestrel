/*
 * ANT+ BSC (bike speed/cadence, combined sensor) display profile, ported
 * from stravaV10's rf/bsc.c onto sdk-ant's ready-made ant_bsc library
 * (ncs/ant/include/ant_profiles/bsc/ant_bsc.h) -- field/macro names
 * (BSC_PROFILE_speed_rev_count etc.) match stravaV10's own usage exactly.
 * Rollover-aware accumulation logic and reconnect-on-close behavior are a
 * faithful port of rf/bsc.c's calculate_speed()/calculate_cadence()/
 * ant_evt_bsc(), including its 200 rpm / 150 kph sanity caps.
 *
 * Bug found and fixed while porting: stravaV10's own SPEED_COEFFICIENT
 * doesn't divide by BSC_MM_TO_M_FACTOR (1000), which is dimensionally wrong
 * -- WHEEL_CIRCUMFERENCE is in mm, so the un-divided coefficient computes
 * speed 1000x too large (mm/s scaled as if it were already m/s). sdk-ant's
 * own bsc_rx reference sample includes the /1000 term; this port uses that
 * (verified correct: km/h = mm/s * 3600 / 1e6 = mm/s * (36/10) / 1000).
 *
 * Device number/channel number/device type match stravaV10's
 * rf/ant_device_manager.h (BSC_DEVICE_NUMBER=15568, BSC_CHANNEL_NUMBER=1,
 * BSC_DEVICE_TYPE=0x79=combined). Channel 0 is ant_demo.c's wildcard
 * channel; HRM uses channel 2.
 *
 * Not ported: stravaV10's optional USE_ANT_SEARCH block, or the hrm_rx/
 * bsc_rx samples' ant_state_indicator LED helpers -- same reasoning as
 * hrm_demo.c.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ant_error.h"
#include "ant_interface.h"
#include "ant_parameters.h"
#include "ant_channel_config.h"
#include "ant_key_manager.h"
#include "ant_profiles/bsc/ant_bsc.h"

#include "bsc_demo.h"

LOG_MODULE_REGISTER(bsc_demo, LOG_LEVEL_INF);

#define BSC_CHANNEL_NUMBER      1
#define BSC_NETWORK_NUMBER      0 /* ANTPLUS_NETWORK_NUMBER in stravaV10 */
#define BSC_DEVICE_NUMBER       15568U
#define WILDCARD_TRANSMISSION_TYPE 0

/* Bike wheel circumference [mm], same value as stravaV10's parameters.h. */
#define WHEEL_CIRCUMFERENCE  2070
#define BSC_EVT_TIME_FACTOR  1024 /* Time unit factor for BSC events. */
#define BSC_RPM_TIME_FACTOR  60   /* Time unit factor for RPM unit. */
#define BSC_MS_TO_KPH_NUM    36   /* Numerator of [m/s] to [kph] ratio. */
#define BSC_MS_TO_KPH_DEN    10   /* Denominator of [m/s] to [kph] ratio. */
#define BSC_MM_TO_M_FACTOR   1000 /* Unit factor [mm/s] to [m/s]. */

#define CADENCE_COEFFICIENT (BSC_EVT_TIME_FACTOR * BSC_RPM_TIME_FACTOR)
#define SPEED_COEFFICIENT                                                                        \
	(WHEEL_CIRCUMFERENCE * BSC_EVT_TIME_FACTOR * BSC_MS_TO_KPH_NUM / BSC_MS_TO_KPH_DEN /       \
	 BSC_MM_TO_M_FACTOR)

static void ant_bsc_evt_handler(ant_bsc_profile_t *p_profile, ant_bsc_evt_t event);

BSC_DISP_CHANNEL_CONFIG_DEF(m_bsc, BSC_CHANNEL_NUMBER, WILDCARD_TRANSMISSION_TYPE,
			     BSC_COMBINED_DEVICE_TYPE, BSC_DEVICE_NUMBER, BSC_NETWORK_NUMBER,
			     BSC_MSG_PERIOD_COMBINED);
BSC_DISP_PROFILE_CONFIG_DEF(m_bsc, ant_bsc_evt_handler);

static ant_bsc_profile_t m_bsc;

typedef struct {
	uint8_t is_init;
	int32_t acc_rev_cnt;
	int32_t prev_rev_cnt;
	int32_t prev_acc_rev_cnt;
	int32_t acc_evt_time;
	int32_t prev_evt_time;
	int32_t prev_acc_evt_time;
} bsc_calc_data_t;

static bsc_calc_data_t m_speed_calc_data;
static bsc_calc_data_t m_cadence_calc_data;

/* Ported values, in place of stravaV10's g_structs.h bsc_info -- Model.cpp
 * isn't wired into stravaV11_fw yet, so this demo just logs. */
static uint32_t m_speed;
static uint32_t m_cadence;

static uint8_t m_reconn_counts;
static uint8_t m_paired;

static void bsc_connect(void)
{
	int err = ant_bsc_disp_open(&m_bsc);

	if (err) {
		LOG_ERR("ant_bsc_disp_open() failed: %d", err);
		return;
	}
	LOG_INF("BSC search restarted");
}

static uint32_t calculate_cadence(int32_t rev_cnt, int32_t evt_time)
{
	uint32_t computed_cadence = 0;

	if (!m_cadence_calc_data.is_init) {
		m_cadence_calc_data.is_init = 1;
		m_cadence_calc_data.prev_rev_cnt = rev_cnt;
		m_cadence_calc_data.prev_evt_time = evt_time;
		return computed_cadence;
	}

	if (rev_cnt != m_cadence_calc_data.prev_rev_cnt) {
		m_cadence_calc_data.acc_rev_cnt += rev_cnt - m_cadence_calc_data.prev_rev_cnt;
		m_cadence_calc_data.acc_evt_time += evt_time - m_cadence_calc_data.prev_evt_time;

		if (m_cadence_calc_data.prev_rev_cnt > rev_cnt) {
			m_cadence_calc_data.acc_rev_cnt += UINT16_MAX + 1;
		}
		if (m_cadence_calc_data.prev_evt_time > evt_time) {
			m_cadence_calc_data.acc_evt_time += UINT16_MAX + 1;
		}

		m_cadence_calc_data.prev_rev_cnt = rev_cnt;
		m_cadence_calc_data.prev_evt_time = evt_time;

		computed_cadence = CADENCE_COEFFICIENT *
				   (m_cadence_calc_data.acc_rev_cnt -
				    m_cadence_calc_data.prev_acc_rev_cnt) /
				   (m_cadence_calc_data.acc_evt_time -
				    m_cadence_calc_data.prev_acc_evt_time);

		m_cadence_calc_data.prev_acc_rev_cnt = m_cadence_calc_data.acc_rev_cnt;
		m_cadence_calc_data.prev_acc_evt_time = m_cadence_calc_data.acc_evt_time;
	}

	if (computed_cadence > 200) {
		computed_cadence = 0;
	}

	return computed_cadence;
}

static uint32_t calculate_speed(int32_t rev_cnt, int32_t evt_time)
{
	uint32_t computed_speed = 0;

	if (!m_speed_calc_data.is_init) {
		m_speed_calc_data.is_init = 1;
		m_speed_calc_data.prev_rev_cnt = rev_cnt;
		m_speed_calc_data.prev_evt_time = evt_time;
		return computed_speed;
	}

	if (rev_cnt != m_speed_calc_data.prev_rev_cnt) {
		m_speed_calc_data.acc_rev_cnt += rev_cnt - m_speed_calc_data.prev_rev_cnt;
		m_speed_calc_data.acc_evt_time += evt_time - m_speed_calc_data.prev_evt_time;

		if (m_speed_calc_data.prev_rev_cnt > rev_cnt) {
			m_speed_calc_data.acc_rev_cnt += UINT16_MAX + 1;
		}
		if (m_speed_calc_data.prev_evt_time > evt_time) {
			m_speed_calc_data.acc_evt_time += UINT16_MAX + 1;
		}

		m_speed_calc_data.prev_rev_cnt = rev_cnt;
		m_speed_calc_data.prev_evt_time = evt_time;

		computed_speed = SPEED_COEFFICIENT *
				 (m_speed_calc_data.acc_rev_cnt -
				  m_speed_calc_data.prev_acc_rev_cnt) /
				 (m_speed_calc_data.acc_evt_time -
				  m_speed_calc_data.prev_acc_evt_time);

		m_speed_calc_data.prev_acc_rev_cnt = m_speed_calc_data.acc_rev_cnt;
		m_speed_calc_data.prev_acc_evt_time = m_speed_calc_data.acc_evt_time;
	}

	if (computed_speed > 150) {
		computed_speed = 0;
	}

	return computed_speed;
}

static void ant_bsc_evt_handler(ant_bsc_profile_t *p_profile, ant_bsc_evt_t event)
{
	if (event != ANT_BSC_COMB_PAGE_0_UPDATED) {
		return;
	}

	m_speed = calculate_speed(p_profile->BSC_PROFILE_speed_rev_count,
				   p_profile->BSC_PROFILE_speed_event_time);
	m_cadence = calculate_cadence(p_profile->BSC_PROFILE_cadence_rev_count,
				       p_profile->BSC_PROFILE_cadence_event_time);

	LOG_INF("BSC speed=%u cadence=%u", m_speed, m_cadence);
}

static void ant_evt_bsc(ant_evt_t *p_ant_evt)
{
	if (p_ant_evt->channel != BSC_CHANNEL_NUMBER) {
		return;
	}

	switch (p_ant_evt->event) {
	case EVENT_RX:
		m_reconn_counts = 0;
		if (!m_paired) {
			uint16_t device_number = 0;
			uint8_t device_type = 0, transmit_type = 0;

			ant_channel_id_get(BSC_CHANNEL_NUMBER, &device_number, &device_type,
					    &transmit_type);
			if (device_number) {
				m_paired = 1;
				LOG_INF("BSC paired with device %u", device_number);
				memset(&m_speed_calc_data, 0, sizeof(m_speed_calc_data));
				memset(&m_cadence_calc_data, 0, sizeof(m_cadence_calc_data));
			}
		}
		ant_bsc_disp_evt_handler(p_ant_evt, &m_bsc);
		break;
	case EVENT_RX_FAIL_GO_TO_SEARCH:
		memset(&m_speed_calc_data, 0, sizeof(m_speed_calc_data));
		memset(&m_cadence_calc_data, 0, sizeof(m_cadence_calc_data));
		break;
	case EVENT_CHANNEL_CLOSED:
		LOG_INF("BSC channel closed");
		m_paired = 0;
		if (m_reconn_counts < 5) {
			m_reconn_counts++;
			bsc_connect();
		}
		break;
	default:
		break;
	}
}

int bsc_demo_start(void)
{
	int err = ant_plus_key_set(BSC_NETWORK_NUMBER);

	if (err) {
		LOG_ERR("ant_plus_key_set() failed: %d", err);
		return err;
	}

	err = ant_bsc_disp_init(&m_bsc, BSC_DISP_CHANNEL_CONFIG(m_bsc),
				 BSC_DISP_PROFILE_CONFIG(m_bsc));
	if (err) {
		LOG_ERR("ant_bsc_disp_init() failed: %d", err);
		return err;
	}

	err = ant_cb_register(&ant_evt_bsc);
	if (err) {
		LOG_ERR("ant_cb_register() failed: %d", err);
		return err;
	}

	err = ant_bsc_disp_open(&m_bsc);
	if (err) {
		LOG_ERR("ant_bsc_disp_open() failed: %d", err);
		return err;
	}

	LOG_INF("ANT+ BSC channel %u open, searching for device %u...", BSC_CHANNEL_NUMBER,
		BSC_DEVICE_NUMBER);

	return 0;
}

uint32_t bsc_demo_get_speed(void)
{
	return m_speed;
}

uint32_t bsc_demo_get_cadence(void)
{
	return m_cadence;
}

bool bsc_demo_is_paired(void)
{
	return m_paired != 0;
}
