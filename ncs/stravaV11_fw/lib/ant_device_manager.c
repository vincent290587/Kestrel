#include "ant_device_manager.h"

void ant_dm_candidates_reset(struct ant_dm_candidate_list *list)
{
	list->count = 0;
}

void ant_dm_candidates_add(struct ant_dm_candidate_list *list, uint16_t dev_id, int8_t rssi)
{
	for (uint16_t i = 0; i < list->count; i++) {
		if (list->candidates[i].dev_id == dev_id) {
			list->candidates[i].rssi = rssi;
			return;
		}
	}

	if (list->count >= ANT_DM_MAX_CANDIDATES) {
		return;
	}

	list->candidates[list->count].dev_id = dev_id;
	list->candidates[list->count].rssi = rssi;
	list->count++;
}
