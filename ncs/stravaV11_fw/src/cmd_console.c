/*
 * Single command listener/dispatcher shared by two transports -- see
 * cmd_console.h for why this replaces gps_sim_demo.c's old RTT-only
 * listener and usb_cmd_demo.c's old CDC-ACM-only one. Each transport gets
 * its own struct cmd_source (line buffer + a small read_byte() adapter
 * normalizing that transport's read call to a common 0-on-byte-read
 * convention), but both are drained by the exact same
 * cmd_source_poll_work_handler() and dispatch through the exact same
 * handle_command() -- the thing being shared is the actual logic, not
 * just the command list.
 *
 * GFX port Phase D (Vue assembly): "SIM START"/"SIM STOP" and their
 * backing gps_sim_demo.c/gps_sim_route.h were removed at the time --
 * assembling the real `Vue` class (Vue.cpp) made the linker actually
 * include the font-rendering/screen-drawing code it had previously
 * eliminated as dead, overflowing this board's flash partition by ~25KB;
 * gps_sim_route.h's embedded 1200-point GPX replay array (~33KB of
 * `static const` data, baked straight into a header) was the single
 * largest deliberately-removable thing in the image, more than covering
 * the overflow on its own.
 *
 * 2026-09-11 (later): "SIM START"/"SIM STOP" are back, backed by a new,
 * much smaller gps_sim_demo.c -- a procedurally-computed straight-line
 * track (a few dozen bytes of state: lat/lon/heading/speed/clock), not an
 * embedded waypoint array, so this doesn't reopen the flash-budget
 * problem above. Feeds real, checksummed synthetic NMEA (RMC+GGA) through
 * Locator's own locator_encode_char() -- see gps_sim_demo.h's own comment
 * for why that's the right entry point (not the position-only
 * gps_demo_inject_location() shim, which never touches TinyGPS++'s own
 * date/time fields that "RIDE START"'s own GPS-time-lock guard needs).
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>

#if defined(CONFIG_USE_SEGGER_RTT)
#include <SEGGER_RTT.h>
#endif

#include "sd_stress_demo.h"
#include "map_screen_demo.h"
#include "disk_raw_test.h"
#include "sd_format.h"
#include "notifications_demo.h"
#ifdef ANT_ENABLED
#include "ant_dm_demo.h"
#endif
#include "stc3100_demo.h"
#include "smp_demo.h"
#include "ble_demo.h"
#include "settings_demo.h"
#include "fram_test.h"
#include "ride_recorder.h"
#include "gps_demo.h"
#include "lezyne_ble.h"
#include "lezyne_handler.h"
#include "gps_sim_demo.h"
#include "vue_demo.h"
#include "cmd_console.h"

LOG_MODULE_REGISTER(cmd_console, LOG_LEVEL_INF);

#define CMD_POLL_INTERVAL_MS 100
#define CMD_BUF_SIZE         32

struct cmd_source {
	char buf[CMD_BUF_SIZE];
	size_t len;
	int (*read_byte)(unsigned char *c); /* 0 = got a byte, nonzero = none available */
	struct k_work_delayable poll_work;
};

static void handle_command(const char *cmd)
{
	if (strcmp(cmd, "STRESS START") == 0) {
		sd_stress_demo_start();
	} else if (strcmp(cmd, "STRESS STOP") == 0) {
		sd_stress_demo_stop();
	} else if (strcmp(cmd, "SIM START") == 0) {
		gps_sim_demo_start();
	} else if (strcmp(cmd, "SIM STOP") == 0) {
		gps_sim_demo_stop();
	} else if (strcmp(cmd, "MAP START") == 0) {
		map_screen_demo_start();
	} else if (strcmp(cmd, "MAP STOP") == 0) {
		map_screen_demo_stop();
	} else if (strcmp(cmd, "DISK TEST") == 0) {
		disk_raw_test_start();
	} else if (strcmp(cmd, "FORMAT SD") == 0) {
		sd_format_start();
	} else if (strcmp(cmd, "LED RED") == 0) {
		notifications_demo_trigger_red();
	} else if (strcmp(cmd, "LED GREEN") == 0) {
		notifications_demo_trigger_green();
	} else if (strcmp(cmd, "LED BLUE") == 0) {
		notifications_demo_trigger_blue();
#ifdef ANT_ENABLED
	} else if (strcmp(cmd, "DM SEARCH HRM") == 0) {
		ant_dm_demo_search_start(ANT_DM_SENSOR_HRM);
	} else if (strcmp(cmd, "DM SEARCH BSC") == 0) {
		ant_dm_demo_search_start(ANT_DM_SENSOR_BSC);
	} else if (strcmp(cmd, "DM SEARCH FEC") == 0) {
		ant_dm_demo_search_start(ANT_DM_SENSOR_FEC);
	} else if (strcmp(cmd, "DM LIST") == 0) {
		ant_dm_demo_search_list();
	} else if (strncmp(cmd, "DM PICK ", 8) == 0) {
		ant_dm_demo_search_validate(atoi(&cmd[8]));
	} else if (strcmp(cmd, "DM CANCEL") == 0) {
		ant_dm_demo_search_cancel();
#endif
	} else if (strcmp(cmd, "BATT") == 0) {
		stc3100_demo_log_reading();
	} else if (strcmp(cmd, "BLE STATUS") == 0) {
		smp_demo_log_status();
	} else if (strcmp(cmd, "CPS STATUS") == 0) {
		ble_demo_log_status();
	} else if (strcmp(cmd, "FRAM TEST") == 0) {
		fram_test_start();
	} else if (strcmp(cmd, "SETTINGS DUMP") == 0) {
		settings_demo_dump();
	} else if (strcmp(cmd, "SETTINGS RESET") == 0) {
		settings_demo_reset();
	} else if (strncmp(cmd, "SETTINGS SET FTP ", 17) == 0) {
		settings_demo_set_ftp((uint16_t)atoi(&cmd[17]));
	} else if (strncmp(cmd, "SETTINGS SET WEIGHT ", 20) == 0) {
		settings_demo_set_weight((uint16_t)atoi(&cmd[20]));
	} else if (strncmp(cmd, "SETTINGS SET HRM ", 17) == 0) {
		settings_demo_set_hrm((uint16_t)atoi(&cmd[17]));
	} else if (strncmp(cmd, "SETTINGS SET BSC ", 17) == 0) {
		settings_demo_set_bsc((uint16_t)atoi(&cmd[17]));
	} else if (strncmp(cmd, "SETTINGS SET FEC ", 17) == 0) {
		settings_demo_set_fec((uint16_t)atoi(&cmd[17]));
	} else if (strncmp(cmd, "SETTINGS SET GLA ", 17) == 0) {
		settings_demo_set_gla((uint16_t)atoi(&cmd[17]));
	} else if (strcmp(cmd, "RIDE START") == 0) {
		uint32_t ts;

		/* No buttons/Boucle yet to drive this from real UI input (see
		 * ride_recorder.h/CLAUDE.md) -- a console command stands in,
		 * same precedent as "DM SEARCH"/"SIM START" before it. Refuses
		 * rather than starting with a bogus timestamp if GPS hasn't
		 * locked time yet -- the FIT filename and file_id.time_created
		 * both depend on it. */
		if (!gps_demo_get_unix_timestamp(&ts)) {
			LOG_WRN("cmd_console: RIDE START refused, no GPS time lock yet");
		} else {
			ride_recorder_start(ts);
		}
	} else if (strcmp(cmd, "RIDE STOP") == 0) {
		ride_recorder_stop();
	} else if (strcmp(cmd, "LAP") == 0) {
		ride_recorder_lap();
	} else if (strcmp(cmd, "VUE LAP") == 0) {
		vue_demo_set_mode(VUE_MODE_LAP);
	} else if (strcmp(cmd, "VUE DEBUG") == 0) {
		vue_demo_set_mode(VUE_MODE_DEBUG);
	} else if (strcmp(cmd, "LEZ STATUS") == 0) {
		lezyne_ble_log_status();
		lezyne_handler_log_status();
	} else if (strcmp(cmd, "MSC MOUNT") == 0) {
		lezyne_handler_msc_mount();
	} else if (strcmp(cmd, "MSC UNMOUNT") == 0) {
		lezyne_handler_msc_unmount();
	} else {
		LOG_WRN("cmd_console: unknown command \"%s\" (try "
			"\"STRESS START\", \"STRESS STOP\", \"SIM START\", \"SIM STOP\", \"MAP START\", \"MAP STOP\", "
			"\"DISK TEST\", \"FRAM TEST\", \"FORMAT SD\", \"LED RED\", \"LED GREEN\", "
			"\"LED BLUE\", "
#ifdef ANT_ENABLED
			"\"DM SEARCH HRM/BSC/FEC\", \"DM LIST\", \"DM PICK <n>\", \"DM CANCEL\", "
#endif
			"\"BATT\", \"BLE STATUS\", \"CPS STATUS\", \"SETTINGS DUMP\", "
			"\"SETTINGS RESET\", \"SETTINGS SET FTP/WEIGHT/HRM/BSC/FEC/GLA <n>\", "
			"\"RIDE START\", \"RIDE STOP\", \"LAP\", \"VUE LAP\", \"VUE DEBUG\", "
			"\"LEZ STATUS\", \"MSC MOUNT\", or \"MSC UNMOUNT\")",
			cmd);
	}
}

static void cmd_source_poll_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct cmd_source *src = CONTAINER_OF(dwork, struct cmd_source, poll_work);
	unsigned char c;

	while (src->read_byte(&c) == 0) {
		if (c == '\n' || c == '\r') {
			if (src->len > 0) {
				src->buf[src->len] = '\0';
				handle_command(src->buf);
				src->len = 0;
			}
		} else if (src->len < CMD_BUF_SIZE - 1) {
			src->buf[src->len++] = (char)c;
		} else {
			/* Line too long: drop it silently, wait for newline. */
			src->len = 0;
		}
	}

	k_work_schedule(&src->poll_work, K_MSEC(CMD_POLL_INTERVAL_MS));
}

/* --- USB CDC-ACM source --- */

static const struct device *s_cdc_dev;

static int cdc_read_byte(unsigned char *c)
{
	return uart_poll_in(s_cdc_dev, c);
}

static struct cmd_source s_cdc_source = {
	.read_byte = cdc_read_byte,
};

/* --- RTT down-channel-0 source --- */

#if defined(CONFIG_USE_SEGGER_RTT)
#define RTT_CMD_CHANNEL 0

static int rtt_read_byte(unsigned char *c)
{
	return (SEGGER_RTT_Read(RTT_CMD_CHANNEL, c, 1) == 1) ? 0 : -1;
}

static struct cmd_source s_rtt_source = {
	.read_byte = rtt_read_byte,
};
#endif

void cmd_console_start(void)
{
	s_cdc_dev = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);

	if (!device_is_ready(s_cdc_dev)) {
		LOG_ERR("cmd_console: CDC-ACM device not ready, USB command source disabled");
	} else {
		/* Confirmed by reading usbd_cdc_acm.c directly: this driver's
		 * bulk-OUT endpoint is never armed to receive host data until
		 * uart_irq_rx_enable() is called at least once -- plain
		 * uart_poll_in() alone silently never receives anything. Safe
		 * without also registering a callback via
		 * uart_irq_callback_set(): every callback-invoking path in
		 * that driver already guards on the callback being non-NULL. */
		uart_irq_rx_enable(s_cdc_dev);

		k_work_init_delayable(&s_cdc_source.poll_work, cmd_source_poll_work_handler);
		k_work_schedule(&s_cdc_source.poll_work, K_NO_WAIT);
	}

#if defined(CONFIG_USE_SEGGER_RTT)
	k_work_init_delayable(&s_rtt_source.poll_work, cmd_source_poll_work_handler);
	k_work_schedule(&s_rtt_source.poll_work, K_NO_WAIT);
#endif

	LOG_INF("cmd_console: ready on RTT down channel 0 and USB CDC-ACM -- "
		"\"STRESS START\", \"STRESS STOP\", \"SIM START\", \"SIM STOP\", \"MAP START\", \"MAP STOP\", "
		"\"DISK TEST\", \"FRAM TEST\", \"FORMAT SD\", \"LED RED\", \"LED GREEN\", \"LED BLUE\", "
#ifdef ANT_ENABLED
		"\"DM SEARCH HRM/BSC/FEC\", \"DM LIST\", \"DM PICK <n>\", \"DM CANCEL\", "
#endif
		"\"BATT\", "
		"\"SETTINGS DUMP\", \"SETTINGS RESET\", \"SETTINGS SET FTP/WEIGHT/HRM/BSC/FEC/GLA <n>\", "
		"\"RIDE START\", \"RIDE STOP\", \"LEZ STATUS\"");
}
