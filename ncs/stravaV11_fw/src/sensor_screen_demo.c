/*
 * Phase 11: periodically pushes live sensor values to the LS027 via
 * gfx_demo_show_sensors() -- same k_work_delayable self-resubmission
 * pattern as poll_demo.c (Phase 8), but runs indefinitely: a live data
 * screen is this feature's actual intended behavior, not a one-shot
 * hardware proof like the other phase demos.
 *
 * GFX port Phase C: reads through model_glue.h instead of calling
 * hrm_demo/bsc_demo/gps_demo/stc3100_demo directly -- the "paired"
 * booleans this passes to gfx_demo_show_sensors() now genuinely reflect
 * data freshness (model_glue's age check), not just whether the ANT+
 * channel has ever paired. A sensor that's gone out of range without its
 * channel formally closing (hrm_demo_is_paired()/bsc_demo_is_paired()
 * alone can't tell that apart -- see model_glue.h's own note) now
 * correctly shows as stale on this screen instead of a frozen last-known
 * reading with no indication anything's wrong.
 */

#include <stdbool.h>

#include <zephyr/kernel.h>

#include "model_glue.h"
#include "gfx_demo.h"

#include "sensor_screen_demo.h"

#define SENSOR_SCREEN_REFRESH_MS 1000

static void sensor_screen_work_handler(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(sensor_screen_work, sensor_screen_work_handler);

static void sensor_screen_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	uint8_t bpm = 0;
	uint16_t rr_ms = 0;
	bool has_hr = model_glue_get_hr_bpm(&bpm);

	(void)model_glue_get_hr_rr_ms(&rr_ms); /* same freshness gate as bpm, always in lockstep */

	uint32_t speed_kph = 0;
	uint32_t cadence_rpm = 0;
	bool has_bsc = model_glue_get_speed_kph(&speed_kph);

	(void)model_glue_get_cadence_rpm(&cadence_rpm); /* same freshness gate as speed */

	float alt_m = 0.f;
	bool has_alt = model_glue_get_gps_altitude(&alt_m);

	float batt_percent = 0.f;
	bool has_batt = model_glue_get_battery_percent(&batt_percent);

	gfx_demo_show_sensors(bpm, rr_ms, has_hr, speed_kph, cadence_rpm, has_bsc, alt_m, has_alt,
			      batt_percent, has_batt);

	k_work_schedule(&sensor_screen_work, K_MSEC(SENSOR_SCREEN_REFRESH_MS));
}

void sensor_screen_demo_start(void)
{
	k_work_schedule(&sensor_screen_work, K_MSEC(SENSOR_SCREEN_REFRESH_MS));
}
