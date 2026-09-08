#include "model_glue.h"

#include "hrm_demo.h"
#include "bsc_demo.h"
#include "fec_demo.h"
#include "stc3100_demo.h"
#include "gps_demo.h"

bool model_glue_get_hr_bpm(uint8_t *bpm)
{
	if (!hrm_demo_is_paired() || hrm_demo_get_age_ms() >= MODEL_GLUE_STALE_MS) {
		return false;
	}

	*bpm = hrm_demo_get_bpm();
	return true;
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
	if (!bsc_demo_is_paired() || bsc_demo_get_age_ms() >= MODEL_GLUE_STALE_MS) {
		return false;
	}

	*cadence_rpm = bsc_demo_get_cadence();
	return true;
}

bool model_glue_get_power_w(uint16_t *power_w)
{
	if (!fec_demo_is_paired() || fec_demo_get_power_age_ms() >= MODEL_GLUE_STALE_MS) {
		return false;
	}

	*power_w = fec_demo_get_power_w();
	return true;
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
