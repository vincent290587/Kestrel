/*
 * See cadence_provider.h for the design. Same pattern as
 * power_provider.c, extended to a third source (ANT+ BSC).
 */

#include "cadence_provider.h"

#include "ble_demo.h"
#include "bsc_demo.h"
#include "fec_demo.h"

#define CADENCE_PROVIDER_STALE_MS 5000 /* matches model_glue.h's MODEL_GLUE_STALE_MS */

enum cadence_source {
	CADENCE_SOURCE_NONE,
	CADENCE_SOURCE_BLE,
	CADENCE_SOURCE_BSC,
	CADENCE_SOURCE_FEC,
};

static enum cadence_source s_source;

bool cadence_provider_get_rpm(uint32_t *rpm)
{
	bool ble_fresh = ble_demo_get_cadence_age_ms() < CADENCE_PROVIDER_STALE_MS;
	bool bsc_fresh = bsc_demo_is_paired() && bsc_demo_get_age_ms() < CADENCE_PROVIDER_STALE_MS;
	bool fec_fresh =
		fec_demo_is_paired() && fec_demo_get_power_age_ms() < CADENCE_PROVIDER_STALE_MS;

	bool current_still_fresh = (s_source == CADENCE_SOURCE_BLE && ble_fresh) ||
				    (s_source == CADENCE_SOURCE_BSC && bsc_fresh) ||
				    (s_source == CADENCE_SOURCE_FEC && fec_fresh);

	if (!current_still_fresh) {
		if (ble_fresh) {
			s_source = CADENCE_SOURCE_BLE;
		} else if (bsc_fresh) {
			s_source = CADENCE_SOURCE_BSC;
		} else if (fec_fresh) {
			s_source = CADENCE_SOURCE_FEC;
		} else {
			s_source = CADENCE_SOURCE_NONE;
		}
	}

	switch (s_source) {
	case CADENCE_SOURCE_BLE:
		*rpm = ble_demo_get_cadence_rpm();
		return true;
	case CADENCE_SOURCE_BSC:
		*rpm = bsc_demo_get_cadence();
		return true;
	case CADENCE_SOURCE_FEC:
		*rpm = fec_demo_get_cadence_rpm();
		return true;
	default:
		return false;
	}
}
