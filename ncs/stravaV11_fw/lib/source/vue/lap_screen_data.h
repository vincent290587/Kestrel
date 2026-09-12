/*
 * Manual-lap feature (2026-09-12): real wiring for VueLap.cpp's live
 * lap data -- same "shared interface, real adapter lives in src/"
 * pattern as debug_screen_data.h (see that header's own comment).
 * Deliberately NOT routed through model_glue.h: that abstraction's job
 * is per-field staleness for a live sensor-data screen, whereas this
 * just forwards ride_recorder.h's own already-staleness-aware getters
 * directly, matching debug_screen_data.h's own STC3100 precedent.
 * stravaV11_fw-only: ride_recorder.c (laps, ride recording) has no
 * stravaV11_app/native_sim equivalent, so this header -- and VueLap
 * itself -- are a deliberate, documented divergence from that app's
 * separate copy of the Vue tree.
 */

#ifndef SOURCE_VUE_LAP_SCREEN_DATA_H_
#define SOURCE_VUE_LAP_SCREEN_DATA_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Live totals for the currently open lap (or the last lap of the most
 * recently stopped ride, same "still show something" convention as
 * ride_recorder_get_live_totals()). False, outputs untouched, if
 * nothing to report. */
bool lap_screen_get_current_lap(uint32_t *elapsed_s, uint16_t *avg_power_w,
				 uint16_t *normalized_power_w, uint32_t *lap_number);

#define LAP_SCREEN_MAX_RECENT_LAPS 5

struct lap_screen_lap_info {
	uint32_t lap_number;
	uint32_t elapsed_s;
	uint16_t avg_power_w;
	uint16_t normalized_power_w;
};

/* Fills out[0..return value), most-recently-closed lap first, up to
 * LAP_SCREEN_MAX_RECENT_LAPS. Returns 0 if no lap has closed yet. */
uint8_t lap_screen_get_recent_laps(struct lap_screen_lap_info *out);

#ifdef __cplusplus
}
#endif

#endif /* SOURCE_VUE_LAP_SCREEN_DATA_H_ */
