/*
 * See power_provider.h for the design. This is deliberately its own tiny
 * module (not folded into model_glue.c) since ride_recorder.c needs the
 * same source-selection logic and shouldn't have to pull in
 * model_glue.h's whole "screen data" abstraction just to get it.
 */

#include "power_provider.h"

#include "ble_demo.h"
#include "fec_demo.h"

#define POWER_PROVIDER_STALE_MS 5000 /* matches model_glue.h's MODEL_GLUE_STALE_MS */

enum power_source {
	POWER_SOURCE_NONE,
	POWER_SOURCE_BLE,
	POWER_SOURCE_ANT,
};

static enum power_source s_source;

bool power_provider_get_watts(uint16_t *watts)
{
	bool ble_fresh = ble_demo_get_power_age_ms() < POWER_PROVIDER_STALE_MS;
	/* is_paired() is also checked, not just the age -- it correctly
	 * reports "never was paired" in a way a stale age alone can't
	 * distinguish from "was paired a long time ago", same reasoning
	 * model_glue.c's own (now-removed) direct check used to rely on. */
	bool ant_fresh =
		fec_demo_is_paired() && fec_demo_get_power_age_ms() < POWER_PROVIDER_STALE_MS;

	bool current_still_fresh = (s_source == POWER_SOURCE_BLE && ble_fresh) ||
				    (s_source == POWER_SOURCE_ANT && ant_fresh);

	if (!current_still_fresh) {
		if (ble_fresh) {
			s_source = POWER_SOURCE_BLE;
		} else if (ant_fresh) {
			s_source = POWER_SOURCE_ANT;
		} else {
			s_source = POWER_SOURCE_NONE;
		}
	}

	switch (s_source) {
	case POWER_SOURCE_BLE:
		*watts = ble_demo_get_power_w();
		return true;
	case POWER_SOURCE_ANT:
		*watts = fec_demo_get_power_w();
		return true;
	default:
		return false;
	}
}
