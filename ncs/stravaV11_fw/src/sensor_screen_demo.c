/*
 * Phase 11: periodically pushes hrm_demo's/bsc_demo's latest decoded ANT+
 * values to the LS027 via gfx_demo_show_sensors() -- same k_work_delayable
 * self-resubmission pattern as poll_demo.c (Phase 8), but runs indefinitely:
 * a live data screen is this feature's actual intended behavior, not a
 * one-shot hardware proof like the other phase demos.
 */

#include <zephyr/kernel.h>

#include "hrm_demo.h"
#include "bsc_demo.h"
#include "gfx_demo.h"

#include "sensor_screen_demo.h"

#define SENSOR_SCREEN_REFRESH_MS 1000

static void sensor_screen_work_handler(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(sensor_screen_work, sensor_screen_work_handler);

static void sensor_screen_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	gfx_demo_show_sensors(hrm_demo_get_bpm(), hrm_demo_get_rr_ms(), hrm_demo_is_paired(),
			      bsc_demo_get_speed(), bsc_demo_get_cadence(), bsc_demo_is_paired());

	k_work_schedule(&sensor_screen_work, K_MSEC(SENSOR_SCREEN_REFRESH_MS));
}

void sensor_screen_demo_start(void)
{
	k_work_schedule(&sensor_screen_work, K_MSEC(SENSOR_SCREEN_REFRESH_MS));
}
