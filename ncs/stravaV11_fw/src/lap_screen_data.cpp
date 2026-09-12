/*
 * Manual-lap feature (2026-09-12): real wiring for VueLap.cpp's
 * lap_screen_data.h -- see that header's own comment for why this
 * isn't routed through model_glue.h. Just forwards to ride_recorder's
 * own getters, already validated on real hardware.
 */

#include "lap_screen_data.h"
#include "ride_recorder.h"

bool lap_screen_get_current_lap(uint32_t *elapsed_s, uint16_t *avg_power_w,
				 uint16_t *normalized_power_w, uint32_t *lap_number)
{
	return ride_recorder_get_current_lap(elapsed_s, avg_power_w, normalized_power_w, lap_number);
}

uint8_t lap_screen_get_recent_laps(struct lap_screen_lap_info *out)
{
	struct ride_recorder_lap_info laps[LAP_SCREEN_MAX_RECENT_LAPS];
	uint8_t count = ride_recorder_get_recent_laps(laps, LAP_SCREEN_MAX_RECENT_LAPS);

	for (uint8_t i = 0; i < count; i++) {
		out[i].lap_number = laps[i].lap_number;
		out[i].elapsed_s = laps[i].elapsed_s;
		out[i].avg_power_w = laps[i].avg_power_w;
		out[i].normalized_power_w = laps[i].normalized_power_w;
	}

	return count;
}
