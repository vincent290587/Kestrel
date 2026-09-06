/*
 * Phase 11: GPS simulation for lab testing without real sky visibility.
 * Replays the first 200 points of a real GPX route (converted via
 * tools/gpx_to_c.py -> gps_sim_route.h, generated from the user's own
 * recorded ride) at realistic ~1Hz pacing, injecting directly into
 * Locator's gps_loc state via gps_demo_inject_location() -- bypassing
 * NMEA/TinyGPS++ entirely, the user's explicit choice: this module is just
 * a straight array lookup, not a parser. speed/course between consecutive
 * route points were pre-computed once at conversion time (great-circle
 * distance/bearing), not on-device, also per the user's choice.
 *
 * Toggled with "SIM START"/"SIM STOP" over the RTT down channel (channel 0,
 * the same "Terminal" pair pyocd's RTT tooling already talks to for
 * console/log output) rather than USB CDC-ACM: the host's ModemManager
 * treats this board's CDC-ACM interface as a modem-probe candidate
 * (confirmed via `ID_MM_CANDIDATE=1`), stalling every host write ~35s until
 * that's fixed host-side (tracked in todo.md) -- RTT has no such problem
 * and this project's tooling (pyocd) already talks RTT for every capture.
 * Nothing in this port's log/console backends (rtt_console.c,
 * log_backend_rtt.c) ever reads from a down channel, so channel 0's down
 * side is free for this.
 *
 * Runs independently of uart_demo()'s real GPS UART path -- this is a
 * separate, opt-in override, not a replacement; nothing here touches the
 * real UART device at all.
 *
 * The RTT command channel needs CONFIG_USE_SEGGER_RTT, which is only
 * enabled on the custom PCB's board config (its console goes over RTT --
 * see CLAUDE.md Phase 11 -- since its one UART is dedicated to the GPS
 * module). The DK still uses a UART console (Phase 2-10), so this whole
 * feature compiles out to a no-op there rather than failing to build.
 */

#include <stdbool.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#if defined(CONFIG_USE_SEGGER_RTT)
#include <SEGGER_RTT.h>
#endif

#include "gps_sim_route.h"
#include "gps_demo.h"
#include "gps_sim_demo.h"

#define RTT_CMD_CHANNEL 0

LOG_MODULE_REGISTER(gps_sim_demo, LOG_LEVEL_INF);

#define CMD_POLL_INTERVAL_MS 100
#define REPLAY_INTERVAL_MS   1000
#define CMD_BUF_SIZE         32

static char s_cmd_buf[CMD_BUF_SIZE];
static size_t s_cmd_len;
static bool s_sim_active;
static size_t s_route_index;

static void replay_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(replay_work, replay_work_handler);

#if defined(CONFIG_USE_SEGGER_RTT)
static void cmd_poll_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(cmd_poll_work, cmd_poll_work_handler);
#endif

static void handle_command(const char *cmd)
{
	if (strcmp(cmd, "SIM START") == 0) {
		if (!s_sim_active) {
			s_sim_active = true;
			s_route_index = 0;
			k_work_schedule(&replay_work, K_NO_WAIT);
			LOG_INF("gps_sim: started (%d points, ~1Hz)", GPS_SIM_ROUTE_LEN);
		} else {
			LOG_INF("gps_sim: already running");
		}
	} else if (strcmp(cmd, "SIM STOP") == 0) {
		if (s_sim_active) {
			s_sim_active = false;
			LOG_INF("gps_sim: stopped at point %u/%u", (unsigned)s_route_index,
				GPS_SIM_ROUTE_LEN);
		} else {
			LOG_INF("gps_sim: not running");
		}
	} else {
		LOG_WRN("gps_sim: unknown command \"%s\" (try \"SIM START\" / \"SIM STOP\")", cmd);
	}
}

#if defined(CONFIG_USE_SEGGER_RTT)
static void cmd_poll_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	unsigned char c;

	while (SEGGER_RTT_Read(RTT_CMD_CHANNEL, &c, 1) == 1) {
		if (c == '\n' || c == '\r') {
			if (s_cmd_len > 0) {
				s_cmd_buf[s_cmd_len] = '\0';
				handle_command(s_cmd_buf);
				s_cmd_len = 0;
			}
		} else if (s_cmd_len < CMD_BUF_SIZE - 1) {
			s_cmd_buf[s_cmd_len++] = (char)c;
		} else {
			/* Line too long: drop it silently, wait for newline. */
			s_cmd_len = 0;
		}
	}

	k_work_schedule(&cmd_poll_work, K_MSEC(CMD_POLL_INTERVAL_MS));
}
#endif /* CONFIG_USE_SEGGER_RTT */

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

void gps_sim_demo_start(void)
{
#if defined(CONFIG_USE_SEGGER_RTT)
	LOG_INF("gps_sim: ready -- type \"SIM START\" or \"SIM STOP\" over the RTT "
		"down channel %d", RTT_CMD_CHANNEL);
	k_work_schedule(&cmd_poll_work, K_MSEC(CMD_POLL_INTERVAL_MS));
#else
	LOG_WRN("gps_sim: RTT command channel unavailable on this board "
		"(CONFIG_USE_SEGGER_RTT not set) -- feature disabled");
#endif
}
