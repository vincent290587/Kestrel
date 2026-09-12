/*
 * See gps_uart_demo.h for the design. Extracted from main.c's old
 * uart_demo() (Phase 6/11), which only ever read the real GPS module's
 * UART for a fixed 2-second window right after boot -- enough to prove
 * the NMEA parser worked, but not a real continuous feed.
 *
 * Real bug found on real hardware while building this: a first version
 * used cmd_console.c's own k_work_delayable-plus-drain-everything
 * *polling* pattern (uart_poll_in() every 100ms). That works for
 * cmd_console.c's human-typed/USB-buffered input, but not for a
 * continuous 9600-baud NMEA stream -- uart_poll_in() on a real physical
 * UART has essentially no driver-side buffering (the old boot-time
 * uart_demo() only got away with plain uart_poll_in() because it
 * busy-polled it, checking almost continuously), so a 100ms gap between
 * polls (~96 bytes' worth of line time at 9600 baud) overran whatever
 * tiny hardware buffering exists and corrupted most incoming sentences
 * (confirmed: constant "Wrong checksum!" from TinyGPS++ once continuous
 * reading went live, where the original tight busy-loop capture never
 * showed that symptom). Fixed by switching to Zephyr's interrupt-driven
 * UART API (CONFIG_UART_INTERRUPT_DRIVEN -- already enabled globally in
 * this build for cmd_console.c's own CDC-ACM source, and the real
 * arduino_serial UART's nrfx UARTE driver supports it too): the ISR
 * callback drains the hardware FIFO into a ring buffer the instant bytes
 * arrive (the one piece of this that's genuinely latency-sensitive), and
 * the existing periodic work item only drains *that* ring buffer into
 * locator_encode_char() -- decoupling real-time byte capture from
 * processing cadence, which is what a fixed-interval poll alone can't
 * do for a continuous serial stream with a real baud rate.
 *
 * Real GPS and gps_sim_demo.c's simulated GPS both ultimately call the
 * exact same locator_encode_char() (Locator.h), feeding the one shared
 * `Locator locator` object (gps_demo.cpp) -- unlike ANT+/BLE power/HRM/
 * cadence, there's no independent per-source state a "provider" could
 * pick between here; gps_demo.h's existing getters already read that one
 * shared state regardless of which byte source fed it. Running both
 * sources at once would interleave two NMEA character streams into one
 * parser and corrupt both, so gps_sim_demo.c pauses this reader for the
 * duration of a simulated ride instead (gps_uart_demo_pause()/_resume())
 * -- mutual exclusion, not a precedence race. A pause/resume landing
 * mid-sentence needs no special handling: TinyGPS++'s encode() is
 * checksum-verified and resyncs on the next '$' regardless of how the
 * previous sentence ended, so at worst one sentence is lost.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/ring_buffer.h>

#include "gps_demo.h"
#include "Locator.h"
#include "gps_uart_demo.h"

#define GPS_UART_DRAIN_INTERVAL_MS 100 /* ring-buffer-to-locator cadence, not byte-capture
					 * cadence -- see this file's own top comment */
#define GPS_UART_REPORT_EVERY_N_DRAINS 50 /* ~5s -- gps_demo_report()'s own periodic log,
					    * replacing what used to be a single one-shot
					    * call at the end of the old boot-time uart_demo() */

/* A few NMEA sentences' worth -- generous relative to the ~140 bytes an
 * RMC+GGA pair costs, so a full GPS_UART_DRAIN_INTERVAL_MS period's
 * worth of bytes (~960 bytes/s * 0.1s = ~96 bytes at 9600 baud) never
 * comes close to overflowing it even if the drain work item is briefly
 * delayed by system load. */
#define GPS_UART_RING_BUF_SIZE 512

RING_BUF_DECLARE(s_gps_uart_rb, GPS_UART_RING_BUF_SIZE);

static const struct device *s_uart;
static bool s_paused;
static uint32_t s_drain_count;

static void gps_uart_isr_callback(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	while (uart_irq_update(dev) && uart_irq_rx_ready(dev)) {
		uint8_t buf[32];
		int n = uart_fifo_read(dev, buf, sizeof(buf));

		if (n <= 0) {
			break;
		}

		/* Ring buffer full is a real possibility only if the drain
		 * work item stalls for far longer than its own 100ms period
		 * -- silently dropping the overflow (ring_buf_put()'s own
		 * behavior) is the right call here, same as any other
		 * best-effort sensor byte stream in this port. */
		ring_buf_put(&s_gps_uart_rb, buf, (uint32_t)n);
	}
}

static void gps_uart_drain_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(gps_uart_drain_work, gps_uart_drain_work_handler);

static void gps_uart_drain_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	uint8_t byte;

	while (ring_buf_get(&s_gps_uart_rb, &byte, 1) == 1) {
		if (!s_paused) {
			locator_encode_char((char)byte);
		}
	}

	s_drain_count++;
	if (!s_paused && (s_drain_count % GPS_UART_REPORT_EVERY_N_DRAINS) == 0) {
		gps_demo_report();
	}

	k_work_schedule(&gps_uart_drain_work, K_MSEC(GPS_UART_DRAIN_INTERVAL_MS));
}

#if DT_NODE_HAS_PROP(DT_PATH(zephyr_user), gps_reset_gpios)
/* GPS_R/GPS_S/FIX_PIN (custom_board_v3.h) -- only defined on the real
 * PCB's board files, not the DK. See main.c's old gps_pins_release() (now
 * removed, moved here) for the full polarity/history writeup. */
static const struct gpio_dt_spec gps_reset =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), gps_reset_gpios);
static const struct gpio_dt_spec gps_standby =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), gps_standby_gpios);
static const struct gpio_dt_spec gps_fix = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), gps_fix_gpios);

static void gps_pins_release(void)
{
	if (!gpio_is_ready_dt(&gps_reset) || !gpio_is_ready_dt(&gps_standby) ||
	    !gpio_is_ready_dt(&gps_fix)) {
		printk("gps: reset/standby/fix GPIO device not ready\n");
		return;
	}

	gpio_pin_configure_dt(&gps_reset, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&gps_standby, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&gps_fix, GPIO_INPUT);

	printk("gps: reset/standby released, fix pin reads %d\n", gpio_pin_get_dt(&gps_fix));
}
#else
static void gps_pins_release(void)
{
}
#endif

void gps_uart_demo_start(void)
{
	gps_pins_release();

	s_uart = DEVICE_DT_GET(DT_NODELABEL(arduino_serial));

	if (!device_is_ready(s_uart)) {
		printk("arduino_serial: not ready\n");
		return;
	}

	static const char msg[] = "$PMTK220,200*2C\r\n"; /* stravaV10's actual fix-interval cmd format */

	for (size_t i = 0; i < sizeof(msg) - 1; i++) {
		uart_poll_out(s_uart, msg[i]);
	}
	printk("arduino_serial: TX complete (%u bytes)\n", (unsigned)(sizeof(msg) - 1));

	uart_irq_callback_set(s_uart, gps_uart_isr_callback);
	uart_irq_rx_enable(s_uart);

	k_work_schedule(&gps_uart_drain_work, K_NO_WAIT);
}

void gps_uart_demo_pause(void)
{
	s_paused = true;
}

void gps_uart_demo_resume(void)
{
	s_paused = false;
}
