#ifndef MODEL_GLUE_H_
#define MODEL_GLUE_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * GFX port Phase C "model glue" (see todo.md): a thin, read-only shim
 * exposing exactly the per-field data a future ported UI screen needs,
 * pulled from the per-subsystem getters this port already has
 * (hrm_demo/bsc_demo/fec_demo/stc3100_demo/gps_demo) rather than
 * replicating stravaV10's push-via-Boucle Model.cpp architecture. This
 * decouples porting the actual screens (Vue/VueCRS/...) from porting
 * Boucle* -- the single largest remaining item in the whole project.
 *
 * Every accessor follows the same convention gps_demo.h/stc3100_demo.h
 * already established: return true and write the out-param only when the
 * backing data is both present and fresh (updated within
 * MODEL_GLUE_STALE_MS); return false and leave the out-param untouched
 * otherwise. This directly implements the user's stale-data-indicator
 * requirement -- a future render call checks the bool to decide "draw the
 * value" vs. "draw a cross across this field's quadrant", without needing
 * to know about per-subsystem pairing/age details itself.
 *
 * hrm_demo/bsc_demo/fec_demo's own is_paired() flags are NOT sufficient
 * for this on their own: they're only cleared on EVENT_CHANNEL_CLOSED,
 * not on EVENT_RX_SEARCH_TIMEOUT, so a sensor that's gone out of range
 * without the ANT+ channel formally closing stays "paired" with an
 * increasingly stale reading. Each of those modules gained a genuine
 * last-update-age getter (hrm_demo_get_age_ms() etc.) specifically to
 * fix this -- this shim combines is_paired() (still checked, since it
 * correctly reports "never was paired") with the real age check.
 */

#define MODEL_GLUE_STALE_MS 5000

bool model_glue_get_hr_bpm(uint8_t *bpm);
bool model_glue_get_hr_rr_ms(uint16_t *rr_ms);
bool model_glue_get_speed_kph(uint32_t *speed_kph);
bool model_glue_get_cadence_rpm(uint32_t *cadence_rpm);
bool model_glue_get_power_w(uint16_t *power_w);
bool model_glue_get_battery_percent(float *percent);
bool model_glue_get_gps_position(float *lat, float *lon);
bool model_glue_get_gps_altitude(float *alt_m);

#ifdef __cplusplus
}
#endif

#endif /* MODEL_GLUE_H_ */
