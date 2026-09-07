/*
 * USB CDC-ACM command console: gates sd_stress_demo, map_screen_demo, and
 * disk_raw_test_start() behind explicit commands instead of auto-starting
 * them at boot -- all three do real, potentially destructive SD-card I/O:
 * sd_stress_demo and map_screen_demo compete with USB MSC host access on
 * their own recurring schedule (confirmed as a real, not just
 * theoretical, problem -- a genuine host-side bulk copy of ~1900 map tile
 * files over the MSC "SD" LUN slowed to a crawl with sd_stress_demo
 * running concurrently at its 500ms cadence), and disk_raw_test_start()'s
 * "SD" half writes straight to sector 0, the FAT boot sector -- doing
 * that every boot was silently reformatting the card fresh (see
 * disk_raw_test.h), wiping the same map-tile data.
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
#include "disk_raw_test.h"
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
	} else if (strcmp(cmd, "DISK TEST") == 0) {
		disk_raw_test_start();
	} else {
		LOG_WRN("usb_cmd: unknown command \"%s\" (try \"STRESS START\", "
			"\"STRESS STOP\", \"MAP START\", \"MAP STOP\", or \"DISK TEST\")",
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

	/* Confirmed by reading usbd_cdc_acm.c directly: this driver's bulk-OUT
	 * endpoint is never armed to receive host data until
	 * uart_irq_rx_enable() is called at least once (cdc_acm_rx_fifo_work,
	 * which posts the actual receive buffer, is only ever first submitted
	 * from cdc_acm_irq_rx_enable()) -- plain uart_poll_in() alone, with no
	 * prior irq_rx_enable() call, silently never receives anything, no
	 * error either. Safe to call without also registering an IRQ callback
	 * via uart_irq_callback_set(): every callback-invoking path in that
	 * driver already guards on the callback being non-NULL before use. */
	uart_irq_rx_enable(s_uart_dev);

	LOG_INF("usb_cmd: ready -- \"STRESS START\"/\"STRESS STOP\"/\"MAP START\"/\"MAP STOP\"/"
		"\"DISK TEST\" over USB CDC-ACM");
	k_work_schedule(&cmd_poll_work, K_NO_WAIT);
}
