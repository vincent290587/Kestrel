#include "model_glue.h"

#include "hrm_demo.h"
#include "bsc_demo.h"
#include "stc3100_demo.h"
#include "gps_demo.h"
#include "power_provider.h"
#include "hrm_provider.h"
#include "cadence_provider.h"

/* hr_bpm/power_w delegate to hrm_provider.h/power_provider.h (2026-09-12)
 * -- those already do this exact "is it live right now" gating (ANT+ or
 * BLE, whichever is actually connected), so this shim's own job is done
 * simply by forwarding to them instead of re-implementing it against
 * hrm_demo.h/fec_demo.h directly. */
bool model_glue_get_hr_bpm(uint8_t *bpm)
{
	return hrm_provider_get_bpm(bpm);
}

bool model_glue_get_hr_rr_ms(uint16_t *rr_ms)
{
	if (!hrm_demo_is_paired() || hrm_demo_get_age_ms() >= MODEL_GLUE_STALE_MS) {
		return false;
	}

	*rr_ms = hrm_demo_get_rr_ms();
	return true;
}

bool model_glue_get_speed_kph(uint32_t *speed_kph)
{
	if (!bsc_demo_is_paired() || bsc_demo_get_age_ms() >= MODEL_GLUE_STALE_MS) {
		return false;
	}

	*speed_kph = bsc_demo_get_speed();
	return true;
}

bool model_glue_get_cadence_rpm(uint32_t *cadence_rpm)
{
	return cadence_provider_get_rpm(cadence_rpm);
}

bool model_glue_get_power_w(uint16_t *power_w)
{
	return power_provider_get_watts(power_w);
}

bool model_glue_get_battery_percent(float *percent)
{
	if (stc3100_demo_get_age_ms() >= MODEL_GLUE_STALE_MS) {
		return false;
	}

	return stc3100_demo_get_percent(percent);
}

bool model_glue_get_gps_position(float *lat, float *lon)
{
	/* gps_demo.c's own GPS_FIX_STALE_MS gate (same 5000ms value as
	 * MODEL_GLUE_STALE_MS) already implements this convention -- no
	 * extra age check needed here. */
	return gps_demo_get_position(lat, lon);
}

bool model_glue_get_gps_altitude(float *alt_m)
{
	return gps_demo_get_altitude(alt_m);
}
