/*
 * ANT+ pairing/search candidate-list logic, ported from stravaV10's
 * rf/ant_device_manager.cpp -- the hardware-agnostic half only (the
 * dedup/add-to-list bookkeeping in ant_device_manager_search_add()).
 * The background-scan channel handling, search start/validate/cancel
 * orchestration belongs to ant_dm_demo.c instead (same "portable list
 * logic vs. Zephyr plumbing" split ant_fec.{h,c}/fec_demo.c already
 * established for FE-C's wire format vs. channel handling).
 *
 * The four *_DEVICE_NUMBER constants below are carried over unmodified
 * from stravaV10's real rf/ant_device_manager.h (they lived in the same
 * file there, not split out) -- they're UserSettings::resetConfig()'s
 * factory-default fallback, used only until a real search (ant_dm_demo.c)
 * + FRAM-persisted pick overrides them. Deliberately NOT also carrying
 * over that original header's *_CHANNEL_NUMBER constants: ant_dm_demo.c
 * already includes this header and defines its own local
 * HRM_CHANNEL_NUMBER/BSC_CHANNEL_NUMBER/FEC_CHANNEL_NUMBER (same values,
 * different spelling -- 2 vs 0x02 etc.), so adding them here too would be
 * a real macro-redefinition conflict, not just a style mismatch.
 * hrm_demo.c/bsc_demo.c/fec_demo.c each still define their own
 * *_DEVICE_NUMBER locally too, rather than including this header --
 * those are hardcoded to this project's own real paired sensors (see
 * CLAUDE.md's Phase 11 updates), a separate, more specific override that
 * takes priority over these generic factory defaults regardless.
 *
 * UserSettings/FRAM persistence of the chosen device number
 * (stravaV10's ant_device_manager_search_validate() writes into
 * u_settings/FRAM) is still not wired into ant_dm_demo.c's own
 * search_validate() -- UserSettings exists in stravaV11_fw now (see
 * lib/source/model/UserSettings.{h,cpp}, adapters/fram_zephyr.c), but a
 * validated pairing there still only reprograms the live ANT channel for
 * that boot; it doesn't yet persist across a reset. Flagged as real
 * follow-up work.
 */

#ifndef ANT_DEVICE_MANAGER_H_
#define ANT_DEVICE_MANAGER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HRM_DEVICE_NUMBER      17334U
#define BSC_DEVICE_NUMBER      15568U
#define BSC_DEVICE_TYPE        0x79
#define GLASSES_DEVICE_NUMBER  0xFDDA
#define TACX_DEVICE_NUMBER     15568U

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
