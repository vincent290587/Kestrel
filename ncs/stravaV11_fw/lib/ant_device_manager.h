/*
 * ANT+ pairing/search candidate-list logic, ported from stravaV10's
 * rf/ant_device_manager.cpp -- the hardware-agnostic half only (the
 * dedup/add-to-list bookkeeping in ant_device_manager_search_add()).
 * The background-scan channel handling, search start/validate/cancel
 * orchestration, and UserSettings/FRAM persistence stravaV10's version
 * also has all belong to ant_dm_demo.c instead (same "portable list
 * logic vs. Zephyr plumbing" split ant_fec.{h,c}/fec_demo.c already
 * established for FE-C's wire format vs. channel handling).
 *
 * Not ported: UserSettings persistence of the chosen device number
 * (stravaV10's ant_device_manager_search_validate() writes into
 * u_settings/FRAM) -- UserSettings isn't wired into stravaV11_fw at all
 * yet (only stravaV11_app has it, from Phase 1), so a validated pairing
 * here only reprograms the live ANT channel for this boot; it doesn't
 * survive a reset. Flagged as real follow-up work once UserSettings is
 * ported into stravaV11_fw.
 */

#ifndef ANT_DEVICE_MANAGER_H_
#define ANT_DEVICE_MANAGER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ANT_DM_MAX_CANDIDATES 8

struct ant_dm_candidate {
	uint16_t dev_id;
	int8_t rssi;
};

struct ant_dm_candidate_list {
	uint16_t count;
	struct ant_dm_candidate candidates[ANT_DM_MAX_CANDIDATES];
};

void ant_dm_candidates_reset(struct ant_dm_candidate_list *list);

/* Adds a newly-seen device to the list, or refreshes its RSSI if already
 * present -- mirrors stravaV10's own dedup-by-device-id loop. Silently
 * drops the candidate once the list is full, same as stravaV10. */
void ant_dm_candidates_add(struct ant_dm_candidate_list *list, uint16_t dev_id, int8_t rssi);

#ifdef __cplusplus
}
#endif

#endif /* ANT_DEVICE_MANAGER_H_ */
