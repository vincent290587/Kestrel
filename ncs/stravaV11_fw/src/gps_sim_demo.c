/*
 * Phase 11: GPS simulation for lab testing without real sky visibility.
 * Replays the first GPS_SIM_ROUTE_LEN points of a real GPX route
 * (converted via tools/gpx_to_c.py -> gps_sim_route.h, generated from the
 * user's own recorded ride) at realistic ~1Hz pacing, injecting directly into
 * Locator's gps_loc state via gps_demo_inject_location() -- bypassing
 * NMEA/TinyGPS++ entirely, the user's explicit choice: this module is just
 * a straight array lookup, not a parser. speed/course between consecutive
 * route points were pre-computed once at conversion time (great-circle
 * distance/bearing), not on-device, also per the user's choice.
 *
 * Started/stopped via cmd_console.c's "SIM START"/"SIM STOP" commands --
 * this file used to own its own RTT-only command listener (a workaround
 * for a since-fixed host ModemManager issue that made USB CDC-ACM
 * impractical at the time), duplicating the same line-buffering logic
 * usb_cmd_demo.c had for its own commands. Both are gone now in favor of
 * cmd_console.c's single shared listener/dispatcher reachable over both
 * RTT and CDC-ACM -- this file only owns the actual replay logic.
 *
 * Runs independently of uart_demo()'s real GPS UART path -- this is a
 * separate, opt-in override, not a replacement; nothing here touches the
 * real UART device at all.
 */

#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "gps_sim_route.h"
#include "gps_demo.h"
#include "gps_sim_demo.h"

LOG_MODULE_REGISTER(gps_sim_demo, LOG_LEVEL_INF);

#define REPLAY_INTERVAL_MS 1000

static bool s_sim_active;
static size_t s_route_index;

static void replay_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(replay_work, replay_work_handler);

static void replay_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!s_sim_active) {
		return;
	}

	if (s_route_index >= GPS_SIM_ROUTE_LEN) {
		LOG_INF("gps_sim: route complete, stopping");
		s_sim_active = false;
		return;
	}

	const struct gps_sim_point *pt = &gps_sim_route[s_route_index];

	gps_demo_inject_location(pt->lat, pt->lon, pt->alt, pt->speed, pt->course, pt->utc_time,
				  pt->date);
	gps_demo_report();

	s_route_index++;

	k_work_schedule(&replay_work, K_MSEC(REPLAY_INTERVAL_MS));
}

void gps_sim_route_start(void)
{
	if (s_sim_active) {
		LOG_INF("gps_sim: already running");
		return;
	}
	s_sim_active = true;
	s_route_index = 0;
	k_work_schedule(&replay_work, K_NO_WAIT);
	LOG_INF("gps_sim: started (%d points, ~1Hz)", GPS_SIM_ROUTE_LEN);
}

void gps_sim_route_stop(void)
{
	if (!s_sim_active) {
		LOG_INF("gps_sim: not running");
		return;
	}
	s_sim_active = false;
	LOG_INF("gps_sim: stopped at point %u/%u", (unsigned)s_route_index, GPS_SIM_ROUTE_LEN);
}
