/*
 * See hrm_provider.h / power_provider.c (the same pattern, one metric
 * over). Its own tiny module for the same reason power_provider.c is --
 * ride_recorder.c shouldn't need model_glue.h's whole "screen data"
 * abstraction just for this.
 */

#include "hrm_provider.h"

#include "ble_demo.h"
#include "hrm_demo.h"

#define HRM_PROVIDER_STALE_MS 5000 /* matches model_glue.h's MODEL_GLUE_STALE_MS */

enum hrm_source {
	HRM_SOURCE_NONE,
	HRM_SOURCE_BLE,
	HRM_SOURCE_ANT,
};

static enum hrm_source s_source;

bool hrm_provider_get_bpm(uint8_t *bpm)
{
	bool ble_fresh = ble_demo_get_hr_age_ms() < HRM_PROVIDER_STALE_MS;
	bool ant_fresh = hrm_demo_is_paired() && hrm_demo_get_age_ms() < HRM_PROVIDER_STALE_MS;

	bool current_still_fresh = (s_source == HRM_SOURCE_BLE && ble_fresh) ||
				    (s_source == HRM_SOURCE_ANT && ant_fresh);

	if (!current_still_fresh) {
		if (ble_fresh) {
			s_source = HRM_SOURCE_BLE;
		} else if (ant_fresh) {
			s_source = HRM_SOURCE_ANT;
		} else {
			s_source = HRM_SOURCE_NONE;
		}
	}

	switch (s_source) {
	case HRM_SOURCE_BLE:
		*bpm = ble_demo_get_hr_bpm();
		return true;
	case HRM_SOURCE_ANT:
		*bpm = hrm_demo_get_bpm();
		return true;
	default:
		return false;
	}
}
