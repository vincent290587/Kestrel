/*
 * ANT+ device manager, ported from stravaV10's rf/ant_device_manager.cpp
 * -- real pairing/search orchestration, replacing the hand-edited
 * hardcoded device numbers hrm_demo.c/bsc_demo.c/fec_demo.c have used up
 * to now (each of those files' own top-of-file comment documents this
 * exact gap). ant_device_manager.{h,c} carries the portable
 * candidate-list bookkeeping; this file is the Zephyr-specific channel
 * plumbing, following the same split ant_fec.{h,c}/fec_demo.c already
 * established for FE-C.
 *
 * stravaV10's own background-scan (BS) channel is channel 0 -- not
 * reusable here, since ant_demo.c already holds channel 0 open
 * permanently as its own always-on wildcard RX demo channel (Phase 5).
 * This port's BS channel is channel 4 instead (the next free number
 * after wildcard=0/BSC=1/HRM=2/FEC=3); nothing else in this port uses 4
 * yet (glasses.c, stravaV10's own next channel after BS, isn't ported).
 *
 * Sequence, matching stravaV10's ant_device_manager_search_start()/
 * _search_add()/_search_validate()/_search_cancel() one-for-one:
 *   1. search_start(type): resets the candidate list, reprograms the BS
 *      channel's ID to (0, device_type_for(type), wildcard transmission)
 *      via ant_channel_id_set(), opens it.
 *   2. Every EVENT_RX on the BS channel while a search is active decodes
 *      the sender's device ID + RSSI from the extended message fields
 *      (ant_lib_config_set(..._INC_RSSI | ..._INC_DEVICE_ID), enabled
 *      once at start -- same mechanism sdk-ant's own ant_bgnd_scan
 *      sample uses) and adds/updates it in the candidate list.
 *   3. search_list(): logs what's been seen so far, indexed for
 *      search_validate().
 *   4. search_validate(idx): closes the BS channel, then calls
 *      ant_channel_id_set() directly on the real profile channel
 *      (HRM/BSC/FEC) with the chosen device number -- deliberately NOT
 *      closing/reopening that channel first, mirroring stravaV10's own
 *      ant_search_end() exactly (it only closes the BS channel; the
 *      profile channel's ID is reprogrammed live). That channel's own
 *      EVENT_RX handler already resets its "paired" flag and re-reads
 *      the device number via ant_channel_id_get() the next time it
 *      receives anything -- so no changes were needed in hrm_demo.c/
 *      bsc_demo.c/fec_demo.c themselves to pick up the new pairing.
 *
 * Reachable from the shared cmd_console.c command table ("DM SEARCH
 * HRM"/"DM SEARCH BSC"/"DM SEARCH FEC", "DM LIST", "DM PICK <n>", "DM
 * CANCEL") -- same "no menu/UI yet, so a plain-text command stands in"
 * approach gps_sim_demo.c's "SIM START"/"SIM STOP" already established.
 *
 * search_validate() also persists the chosen device number to FRAM via
 * settings_demo_set_hrm()/_bsc()/_fec() (UserSettings::writeConfig()),
 * matching stravaV10's own ant_device_manager_search_validate() (which
 * writes into u_settings/FRAM right after ant_search_end()) -- this was
 * the one piece explicitly deferred when this file was first ported (see
 * ant_device_manager.h's own history), blocked on UserSettings not being
 * wired into stravaV11_fw at all yet. It is now (see
 * lib/source/model/UserSettings.{h,cpp}, adapters/fram_zephyr.c). Note
 * this only persists a *live* "DM PICK" -- it doesn't change where
 * hrm_demo.c/bsc_demo.c/fec_demo.c get their device number at boot
 * (still each file's own hardcoded real constant, a deliberate, separate
 * decision -- see ant_device_manager.h's comment on that).
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ant_error.h"
#include "ant_interface.h"
#include "ant_parameters.h"
#include "ant_channel_config.h"
#include "ant_search_config.h"
#include "ant_key_manager.h"

#include "ant_profiles/hrm/ant_hrm.h"
#include "ant_profiles/bsc/ant_bsc.h"

#include "ant_device_manager.h"
#include "ant_dm_demo.h"
#include "settings_demo.h"

LOG_MODULE_REGISTER(ant_dm_demo, LOG_LEVEL_INF);

#define BS_CHANNEL_NUMBER 4
#define BS_NETWORK_NUMBER 0 /* ANTPLUS_NETWORK_NUMBER in stravaV10 */
#define BS_RF_FREQ 0x39u    /* 2457 MHz -- same ANT+ frequency every channel in this port uses */
#define WILDCARD_TRANSMISSION_TYPE 0

/* FEC_DEVICE_TYPE (0x11) is duplicated from fec_demo.c rather than
 * shared via a header -- fec_demo.c doesn't expose it, and it's a fixed
 * ANT+-spec constant, not something that could drift between the two
 * files independently. */
#define FEC_DEVICE_TYPE 0x11u

#define FEC_CHANNEL_NUMBER 3
#define BSC_CHANNEL_NUMBER 1
#define HRM_CHANNEL_NUMBER 2

static const ant_channel_config_t m_bs_channel_config = {
	.channel_number = BS_CHANNEL_NUMBER,
	.channel_type = CHANNEL_TYPE_SLAVE,
	.ext_assign = EXT_PARAM_ALWAYS_SEARCH,
	.rf_freq = BS_RF_FREQ,
	.transmission_type = WILDCARD_TRANSMISSION_TYPE,
	.device_type = 0, /* wild card -- narrowed per-search via ant_channel_id_set() */
	.device_number = 0,
	.channel_period = 0, /* not taken into account for a background-scan channel */
	.network_number = BS_NETWORK_NUMBER,
};

static const ant_search_config_t m_bs_search_config = {
	.channel_number = BS_CHANNEL_NUMBER,
	.low_priority_timeout = ANT_LOW_PRIORITY_TIMEOUT_DISABLE,
	.high_priority_timeout = 80, /* ANT_DEVICE_MANAGER's own value in stravaV10, ~200s */
	.search_sharing_cycles = ANT_SEARCH_SHARING_CYCLES_DISABLE,
	.search_priority = ANT_SEARCH_PRIORITY_DEFAULT,
	.waveform = ANT_WAVEFORM_DEFAULT,
};

static struct ant_dm_candidate_list m_candidates;
static enum ant_dm_sensor_type m_search_type = ANT_DM_SENSOR_NONE;

static uint8_t device_type_for(enum ant_dm_sensor_type type)
{
	switch (type) {
	case ANT_DM_SENSOR_HRM:
		return HRM_DEVICE_TYPE;
	case ANT_DM_SENSOR_BSC:
		return BSC_COMBINED_DEVICE_TYPE;
	case ANT_DM_SENSOR_FEC:
		return FEC_DEVICE_TYPE;
	default:
		return 0;
	}
}

static uint8_t profile_channel_for(enum ant_dm_sensor_type type)
{
	switch (type) {
	case ANT_DM_SENSOR_HRM:
		return HRM_CHANNEL_NUMBER;
	case ANT_DM_SENSOR_BSC:
		return BSC_CHANNEL_NUMBER;
	case ANT_DM_SENSOR_FEC:
		return FEC_CHANNEL_NUMBER;
	default:
		return 0xff;
	}
}

/* Mirrors stravaV10's ant_device_manager_search_validate() switch on
 * m_search_type, just dispatching to settings_demo's setters instead of
 * writing sUserParameters fields directly -- those already do the
 * "set field, writeConfig()" pair (settings_demo.cpp). */
static void persist_device_id(enum ant_dm_sensor_type type, uint16_t dev_id)
{
	switch (type) {
	case ANT_DM_SENSOR_HRM:
		settings_demo_set_hrm(dev_id);
		break;
	case ANT_DM_SENSOR_BSC:
		settings_demo_set_bsc(dev_id);
		break;
	case ANT_DM_SENSOR_FEC:
		settings_demo_set_fec(dev_id);
		break;
	default:
		break;
	}
}

static const char *sensor_type_name(enum ant_dm_sensor_type type)
{
	switch (type) {
	case ANT_DM_SENSOR_HRM:
		return "HRM";
	case ANT_DM_SENSOR_BSC:
		return "BSC";
	case ANT_DM_SENSOR_FEC:
		return "FEC";
	default:
		return "NONE";
	}
}

static void ant_evt_bs(ant_evt_t *p_ant_evt)
{
	if (p_ant_evt->channel != BS_CHANNEL_NUMBER) {
		return;
	}

	if (p_ant_evt->event != EVENT_RX) {
		return;
	}

	uint16_t dev_id = 0;
	int8_t rssi = 0;

	if (p_ant_evt->message.ANT_MESSAGE_stExtMesgBF.bANTDeviceID) {
		/* Little-endian per sdk-ant's own ant_bgnd_scan sample. */
		dev_id = (uint16_t)(p_ant_evt->message.ANT_MESSAGE_aucExtData[1] << 8) |
			 p_ant_evt->message.ANT_MESSAGE_aucExtData[0];
	}
	if (p_ant_evt->message.ANT_MESSAGE_stExtMesgBF.bANTRssi) {
		rssi = (int8_t)p_ant_evt->message.ANT_MESSAGE_aucExtData[5];
	}

	if (!dev_id) {
		return;
	}

	bool is_new = true;

	for (uint16_t i = 0; i < m_candidates.count; i++) {
		if (m_candidates.candidates[i].dev_id == dev_id) {
			is_new = false;
			break;
		}
	}

	ant_dm_candidates_add(&m_candidates, dev_id, rssi);

	if (is_new) {
		LOG_INF("DM found %s candidate: device %u (rssi %d)",
			sensor_type_name(m_search_type), dev_id, rssi);
	}
}

int ant_dm_demo_start(void)
{
	int err = ant_plus_key_set(BS_NETWORK_NUMBER);

	if (err) {
		LOG_ERR("ant_plus_key_set() failed: %d", err);
		return err;
	}

	err = ant_lib_config_set(ANT_LIB_CONFIG_MESG_OUT_INC_RSSI |
				  ANT_LIB_CONFIG_MESG_OUT_INC_DEVICE_ID);
	if (err) {
		LOG_ERR("ant_lib_config_set() failed: %d", err);
		return err;
	}

	err = ant_channel_init(&m_bs_channel_config);
	if (err) {
		LOG_ERR("ant_channel_init() failed: %d", err);
		return err;
	}

	err = ant_search_init(&m_bs_search_config);
	if (err) {
		LOG_ERR("ant_search_init() failed: %d", err);
		return err;
	}

	err = ant_cb_register(&ant_evt_bs);
	if (err) {
		LOG_ERR("ant_cb_register() failed: %d", err);
		return err;
	}

	LOG_INF("ANT+ device manager ready (background-scan channel %u) -- \"DM SEARCH "
		"HRM/BSC/FEC\", \"DM LIST\", \"DM PICK <n>\", \"DM CANCEL\"",
		BS_CHANNEL_NUMBER);

	return 0;
}

void ant_dm_demo_search_start(enum ant_dm_sensor_type type)
{
	if (type == ANT_DM_SENSOR_NONE) {
		LOG_ERR("DM search: invalid sensor type");
		return;
	}

	/* Unconditional close before reprogramming, matching stravaV10's own
	 * ant_search_start() exactly -- starting a new search while a
	 * previous one is still open (no intervening "DM CANCEL"/"DM PICK")
	 * would otherwise fail ant_channel_open() below (found on real
	 * hardware: a BSC search left the channel open, then starting a FEC
	 * search failed with "ant_channel_open() failed: 16405" since the
	 * channel was never closed first). Closing an already-closed channel
	 * is expected to fail too -- that's not an error worth logging. */
	(void)ant_channel_close(BS_CHANNEL_NUMBER);

	ant_dm_candidates_reset(&m_candidates);
	m_search_type = type;

	int err = ant_channel_id_set(BS_CHANNEL_NUMBER, 0, device_type_for(type),
				      WILDCARD_TRANSMISSION_TYPE);

	if (err) {
		LOG_ERR("ant_channel_id_set() failed: %d", err);
		return;
	}

	err = ant_channel_open(BS_CHANNEL_NUMBER);
	if (err) {
		LOG_ERR("ant_channel_open() failed: %d", err);
		return;
	}

	LOG_INF("DM search started for %s", sensor_type_name(type));
}

void ant_dm_demo_search_list(void)
{
	if (m_candidates.count == 0) {
		LOG_INF("DM candidates: none seen yet");
		return;
	}

	for (uint16_t i = 0; i < m_candidates.count; i++) {
		LOG_INF("DM candidate %u: device %u (rssi %d)", i,
			m_candidates.candidates[i].dev_id, m_candidates.candidates[i].rssi);
	}
}

void ant_dm_demo_search_validate(int idx)
{
	if (m_search_type == ANT_DM_SENSOR_NONE) {
		LOG_ERR("DM pick: no search active");
		return;
	}
	if (idx < 0 || (uint16_t)idx >= m_candidates.count) {
		LOG_ERR("DM pick: invalid index %d (%u candidates)", idx, m_candidates.count);
		return;
	}

	uint16_t dev_id = m_candidates.candidates[idx].dev_id;

	int err = ant_channel_close(BS_CHANNEL_NUMBER);

	if (err) {
		LOG_ERR("ant_channel_close() failed: %d", err);
	}

	uint8_t profile_channel = profile_channel_for(m_search_type);

	err = ant_channel_id_set(profile_channel, dev_id, device_type_for(m_search_type),
				  WILDCARD_TRANSMISSION_TYPE);
	if (err) {
		LOG_ERR("ant_channel_id_set() on channel %u failed: %d", profile_channel, err);
		return;
	}

	LOG_INF("DM %s paired with device %u (channel %u reprogrammed)",
		sensor_type_name(m_search_type), dev_id, profile_channel);

	persist_device_id(m_search_type, dev_id);

	m_search_type = ANT_DM_SENSOR_NONE;
}

void ant_dm_demo_search_cancel(void)
{
	if (m_search_type == ANT_DM_SENSOR_NONE) {
		LOG_INF("DM cancel: no search active");
		return;
	}

	int err = ant_channel_close(BS_CHANNEL_NUMBER);

	if (err) {
		LOG_ERR("ant_channel_close() failed: %d", err);
	}

	LOG_INF("DM search cancelled for %s", sensor_type_name(m_search_type));
	m_search_type = ANT_DM_SENSOR_NONE;
}
