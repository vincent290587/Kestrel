/*
 * USB CDC-ACM command console: gates sd_stress_demo and map_screen_demo
 * behind explicit "STRESS START"/"STRESS STOP"/"MAP START"/"MAP STOP"
 * commands instead of auto-starting them at boot -- both do real SD-card
 * I/O on their own recurring schedule (sd_stress_demo every 500ms,
 * map_screen_demo every ~1s once gps_sim_demo has a fix), competing for
 * the same physical SD card as USB MSC (usb_demo.c's own comment already
 * flags this concurrent-access hazard). Confirmed as a real, not just
 * theoretical, problem: a genuine host-side bulk copy of ~1900 map tile
 * files over the MSC "SD" LUN slowed to a crawl with sd_stress_demo
 * running concurrently at its 500ms cadence.
 *
 * Uses USB CDC-ACM, not the RTT down channel gps_sim_demo.c's "SIM
 * START"/"SIM STOP" commands use -- that module's own comment explains
 * why RTT was chosen over CDC-ACM at the time (the host's ModemManager
 * stalling every CDC-ACM write ~35s). That's since been fixed host-side
 * (a udev rule ignoring this board's VID:PID, see todo.md), so CDC-ACM
 * is a reasonable command channel again. Deliberately not migrating
 * gps_sim_demo's existing RTT control here too -- it isn't the source of
 * the reported problem and already works, so leaving it alone.
 */

#include <stdbool.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>

#include "sd_stress_demo.h"
#include "map_screen_demo.h"
#include "usb_cmd_demo.h"

LOG_MODULE_REGISTER(usb_cmd_demo, LOG_LEVEL_INF);

#define CMD_POLL_INTERVAL_MS 100
#define CMD_BUF_SIZE         32

static char s_cmd_buf[CMD_BUF_SIZE];
static size_t s_cmd_len;
static const struct device *s_uart_dev;

static void cmd_poll_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(cmd_poll_work, cmd_poll_work_handler);

static void handle_command(const char *cmd)
{
	if (strcmp(cmd, "STRESS START") == 0) {
		sd_stress_demo_start();
	} else if (strcmp(cmd, "STRESS STOP") == 0) {
		sd_stress_demo_stop();
	} else if (strcmp(cmd, "MAP START") == 0) {
		map_screen_demo_start();
	} else if (strcmp(cmd, "MAP STOP") == 0) {
		map_screen_demo_stop();
	} else {
		LOG_WRN("usb_cmd: unknown command \"%s\" (try \"STRESS START\", "
			"\"STRESS STOP\", \"MAP START\", or \"MAP STOP\")",
			cmd);
	}
}

static void cmd_poll_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	unsigned char c;

	while (uart_poll_in(s_uart_dev, &c) == 0) {
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

void usb_cmd_demo_start(void)
{
	s_uart_dev = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);

	if (!device_is_ready(s_uart_dev)) {
		LOG_ERR("usb_cmd: CDC-ACM device not ready");
		return;
	}

	LOG_INF("usb_cmd: ready -- \"STRESS START\"/\"STRESS STOP\"/\"MAP START\"/\"MAP STOP\" "
		"over USB CDC-ACM");
	k_work_schedule(&cmd_poll_work, K_NO_WAIT);
}
