/*
 * Phase 2/3 DK bring-up smoke test.
 *
 * Validated two different ways:
 *  - LED, button, I2C bus scan: this DK has the first two on-board and
 *    nothing needs to ACK on the bus for the third, so these are real,
 *    observed hardware results.
 *  - LS027 display, bme280, fxos8700, FRAM (mb85rcxx): nothing is wired up
 *    to this DK (see CLAUDE.md and the board overlay), so these only prove
 *    each driver initializes and completes its transactions without
 *    error/hang -- not that any particular reading or pixel is correct.
 *    That check comes once real hardware is wired up (custom PCB phase).
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/sys/printk.h>

#include "ble_demo.h"
#include "ant_demo.h"
#include "hrm_demo.h"
#include "bsc_demo.h"
#include "gfx_demo.h"
#include "gps_demo.h"
#include "gps_sim_demo.h"
#include "Locator.h"
#include "sensor_screen_demo.h"
#include "task_demo.h"
#include "power_demo.h"
#include "poll_demo.h"
#include "usb_demo.h"

/*
 * The custom PCB latches its own regulator ON via the STC3100 fuel gauge's
 * IO0 pin: source/sensors/STC3100.cpp's reset()/init() sequence (REG_CONTROL
 * = STC_RESET, then REG_MODE = MODE_RUN) leaves IO0 actively driven, holding
 * the latch; shutdown() (REG_MODE = 0, then REG_CONTROL = STC_IO_OD) releases
 * it to open-drain so the board can power itself off -- see
 * power_scheduler.cpp's real stc.shutdown() call and CLAUDE.md's Phase 8
 * notes on that mechanism. No Zephyr driver for the STC3100 exists yet
 * (Phase 3 gap), so until one does, this replicates only stravaV10's own
 * init() register writes -- just enough to hold power, not the full
 * fuel-gauge driver. Found the hard way: the board powered itself off
 * between Phase 11 test runs before this existed, since nothing was ever
 * talking to the STC3100 at all, and its post-reset default apparently
 * doesn't hold the latch on its own. Runs first, before anything else in
 * main() -- every millisecond without this held is a millisecond the board
 * risks losing power. Harmless on the DK: the same i2c_demo()-scanned bus,
 * just cleanly NACKed since nothing is at this address there.
 */
#define STC3100_I2C_ADDR    0x70
#define STC3100_REG_MODE    0
#define STC3100_REG_CONTROL 1
#define STC3100_MODE_RUN    0x10
#define STC3100_CTRL_RESET  0x02

static void stc3100_power_latch_hold(void)
{
	const struct device *i2c = DEVICE_DT_GET(DT_NODELABEL(arduino_i2c));

	if (!device_is_ready(i2c)) {
		return;
	}

	uint8_t reset_cmd[2] = { STC3100_REG_CONTROL, STC3100_CTRL_RESET };
	int err = i2c_write(i2c, reset_cmd, sizeof(reset_cmd), STC3100_I2C_ADDR);

	if (err) {
		/* No STC3100 on this bus (e.g. the DK) -- nothing to hold. */
		return;
	}

	k_msleep(1);

	uint8_t mode_cmd[2] = { STC3100_REG_MODE, STC3100_MODE_RUN };

	i2c_write(i2c, mode_cmd, sizeof(mode_cmd), STC3100_I2C_ADDR);

	printk("stc3100: power latch held (CONTROL reset, MODE run)\n");
}

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

static void led_button_demo(void)
{
	if (!gpio_is_ready_dt(&led) || !gpio_is_ready_dt(&button)) {
		printk("LED or button GPIO device not ready\n");
		return;
	}

	gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&button, GPIO_INPUT);

	bool was_pressed = false;

	for (int i = 0; i < 10; i++) {
		gpio_pin_toggle_dt(&led);

		bool pressed = gpio_pin_get_dt(&button) > 0;
		if (pressed != was_pressed) {
			printk("button0: %s\n", pressed ? "pressed" : "released");
			was_pressed = pressed;
		}

		k_msleep(300);
	}
}

static void i2c_demo(void)
{
	/* No sensor is attached (bare DK) -- this only proves the bus
	 * initializes and issues clean transactions (NACKs are expected and
	 * fine; a hang would not be). */
	const struct device *i2c = DEVICE_DT_GET(DT_NODELABEL(arduino_i2c));

	if (!device_is_ready(i2c)) {
		printk("I2C device not ready\n");
		return;
	}

	int nacks = 0, acks = 0;

	for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
		uint8_t dummy;
		int err = i2c_read(i2c, &dummy, 1, addr);

		if (err == 0) {
			acks++;
		} else {
			nacks++;
		}
	}
	printk("I2C scan (arduino_i2c): %d NACKs, %d ACKs (no sensor attached, so ACKs would be unexpected)\n",
	       nacks, acks);
}

static void sensor_demo(const char *name, const struct device *dev)
{
	if (!device_is_ready(dev)) {
		printk("%s: not ready (expected -- nothing attached)\n", name);
		return;
	}

	int err = sensor_sample_fetch(dev);
	printk("%s: ready, sensor_sample_fetch() -> %d\n", name, err);
}

static void fram_demo(void)
{
	const struct device *fram = DEVICE_DT_GET(DT_NODELABEL(fram));

	if (!device_is_ready(fram)) {
		printk("fram: not ready (expected -- nothing attached)\n");
		return;
	}

	printk("fram: ready, size=%zu bytes\n", eeprom_get_size(fram));

	static const uint8_t pattern[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
	uint8_t readback[sizeof(pattern)] = { 0 };

	int err = eeprom_write(fram, 0, pattern, sizeof(pattern));
	printk("eeprom_write() -> %d\n", err);

	err = eeprom_read(fram, 0, readback, sizeof(readback));
	printk("eeprom_read() -> %d, %s\n", err,
	       memcmp(pattern, readback, sizeof(pattern)) == 0 ? "MATCH" : "MISMATCH");
}

static void disk_raw_ioctl_demo(const char *name)
{
	int err = disk_access_ioctl(name, DISK_IOCTL_CTRL_INIT, NULL);

	if (err != 0) {
		printk("disk %s: init -> %d\n", name, err);
		return;
	}

	uint32_t block_count = 0, block_size = 0;
	disk_access_ioctl(name, DISK_IOCTL_GET_SECTOR_COUNT, &block_count);
	disk_access_ioctl(name, DISK_IOCTL_GET_SECTOR_SIZE, &block_size);
	printk("disk %s: init ok, %u sectors x %u bytes\n", name, block_count, block_size);

	/* Real write/read/verify round trip, same rigor as qspi_flash_demo()
	 * below -- enumeration alone (sector count/size) doesn't prove data
	 * actually moves correctly. Sector 0 is fine here: confirmed with
	 * the user this card is blank/scratch, not carrying real data. */
	if (block_size == 512) {
		static uint8_t pattern[512];
		static uint8_t readback[512];

		for (size_t i = 0; i < sizeof(pattern); i++) {
			pattern[i] = (uint8_t)i;
		}

		err = disk_access_write(name, pattern, 0, 1);
		printk("disk %s: write() -> %d\n", name, err);

		err = disk_access_read(name, readback, 0, 1);
		printk("disk %s: read() -> %d, %s\n", name, err,
		       memcmp(pattern, readback, sizeof(pattern)) == 0 ? "MATCH" : "MISMATCH");
	} else {
		printk("disk %s: unexpected sector size %u, skipping write/read test\n", name,
		       block_size);
	}

	disk_access_ioctl(name, DISK_IOCTL_CTRL_DEINIT, NULL);
}

static void qspi_flash_demo(void)
{
	/* Real, populated hardware: erase/write/read/verify directly against
	 * the DK's on-board mx25r64 QSPI NOR chip (see the overlay for why
	 * this is the direct flash API rather than disk_access/FAT). On the
	 * real PCB, this node is the board's actual external chip -- labeled
	 * mx25r64 for main.c source-compat (see the board .dts), but the
	 * jedec-id configured there (20 ba 18) is Micron's, matching
	 * libraries/SST/mt25.c's own read_id, not an SST/Microchip part
	 * (manufacturer byte would be 0xBF, not 0x20) despite the
	 * "libraries/SST" directory name -- confirmed below by reading the
	 * chip's actual ID back, not just trusting the devicetree config
	 * (which nrf_qspi_nor.c's own init already hard-validates: a
	 * mismatch returns -ENODEV and device_is_ready() would be false). */
	const struct device *flash = DEVICE_DT_GET(DT_NODELABEL(mx25r64));

	if (!device_is_ready(flash)) {
		printk("mx25r64: not ready\n");
		return;
	}

	uint8_t jedec_id[3] = { 0 };
	int jedec_err = flash_read_jedec_id(flash, jedec_id);

	printk("mx25r64: flash_read_jedec_id() -> %d, id=%02x %02x %02x\n", jedec_err,
	       jedec_id[0], jedec_id[1], jedec_id[2]);

	static const uint8_t pattern[16] = {
		0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x11, 0x22, 0x33,
		0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB,
	};
	uint8_t readback[sizeof(pattern)] = { 0 };

	int err = flash_erase(flash, 0, 4096);
	printk("mx25r64: flash_erase() -> %d\n", err);

	err = flash_write(flash, 0, pattern, sizeof(pattern));
	printk("mx25r64: flash_write() -> %d\n", err);

	err = flash_read(flash, 0, readback, sizeof(readback));
	printk("mx25r64: flash_read() -> %d, %s\n", err,
	       memcmp(pattern, readback, sizeof(pattern)) == 0 ? "MATCH" : "MISMATCH");
}

static void storage_demo(void)
{
	/* SD card: no card/slot on this DK (see the overlay) -- this just
	 * proves the driver reports a clean error instead of hanging. */
	disk_raw_ioctl_demo("SD");

	qspi_flash_demo();
}

#if DT_NODE_HAS_PROP(DT_PATH(zephyr_user), gps_reset_gpios)
/* GPS_R/GPS_S/FIX_PIN (custom_board_v3.h) -- only defined on the real PCB's
 * board files (stravav11_nrf52840.dts), not the DK, since the DK has no
 * counterpart pins for them. GPS_R/GPS_S are active-low (TDD/Simulator.cpp
 * only emits NMEA data once both read high), so "released" (module out of
 * reset/standby, its normal running state) means driving both to their
 * GPIO_DT_SPEC-relative INACTIVE level -- physically high. Previously left
 * floating, which is the likely reason the GPS UART received unexplained
 * bytes on every boot (Phase 11): nothing ever held the module in a known
 * state either way. FIX_PIN is active-high, GPS-to-MCU. */
static const struct gpio_dt_spec gps_reset =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), gps_reset_gpios);
static const struct gpio_dt_spec gps_standby =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), gps_standby_gpios);
static const struct gpio_dt_spec gps_fix =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), gps_fix_gpios);

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

static void uart_demo(void)
{
	/* Phase 6: GPS module UART (arduino_serial/uart1, see the overlay).
	 * On the DK, no GPS module is attached -- this only proves TX
	 * completes and RX doesn't hang, not that anything is received. On
	 * the real PCB (Phase 11), gps_pins_release() below takes the module
	 * out of reset/standby first, so RX bytes here are real. Every
	 * received byte is also fed through locator_encode_char() (Phase 6's
	 * TinyGPS++-based Locator, ported unmodified from stravaV11_app) --
	 * see gps_demo_report() below for what it decoded. */
	gps_pins_release();
	gps_demo_init();

	const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(arduino_serial));

	if (!device_is_ready(uart)) {
		printk("arduino_serial: not ready\n");
		return;
	}

	static const char msg[] = "$PMTK220,200*2C\r\n"; /* stravaV10's actual fix-interval cmd format */

	for (size_t i = 0; i < sizeof(msg) - 1; i++) {
		uart_poll_out(uart, msg[i]);
	}
	printk("arduino_serial: TX complete (%u bytes)\n", (unsigned)(sizeof(msg) - 1));

	/* Capture and print the actual bytes, not just a count: on the real
	 * PCB (Phase 11) something answers here even though nothing decodes
	 * it yet. Polls for a wall-clock window, not a fixed iteration count
	 * -- uart_poll_in() is non-blocking, and a fixed spin-count can
	 * finish before a real 9600-baud stream (~73ms for a ~70-byte NMEA
	 * sentence) has had time to arrive. */
	static uint8_t rx_buf[256];
	int rx_count = 0;
	int64_t deadline = k_uptime_get() + 2000;

	while (k_uptime_get() < deadline && rx_count < (int)sizeof(rx_buf)) {
		unsigned char rx;

		if (uart_poll_in(uart, &rx) == 0) {
			rx_buf[rx_count++] = rx;
			locator_encode_char((char)rx);
		} else {
			/* Only sleep when idle, not while bytes are actively
			 * arriving: a tight 2s CPU-bound spin here starved the
			 * deferred-log thread of any chance to run, which
			 * dropped several other boot messages queued during
			 * this window (RTT's own buffering is unrelated --
			 * this is Zephyr's separate deferred-log message pool
			 * filling up because nothing could drain it). */
			k_msleep(1);
		}
	}

	/* Build the whole dump into one buffer and printk() it once -- a
	 * separate printk() per byte flooded the deferred log queue (each one
	 * queued as its own message) badly enough that a first attempt at
	 * this logged "258 messages dropped", losing everything else that
	 * boot was trying to log at the same time. */
	static char dump[4 * sizeof(rx_buf) + 1];
	size_t dump_len = 0;

	for (int i = 0; i < rx_count && dump_len + 5 < sizeof(dump); i++) {
		if (rx_buf[i] >= 0x20 && rx_buf[i] < 0x7f) {
			dump[dump_len++] = (char)rx_buf[i];
		} else {
			dump_len += snprintf(&dump[dump_len], 5, "\\x%02x", rx_buf[i]);
		}
	}
	dump[dump_len] = '\0';

	printk("arduino_serial: RX got %d bytes over 2s: \"%s\"\n", rx_count, dump);

	gps_demo_report();

	/* Give the deferred-log thread a moment to actually drain those two
	 * lines before ant_demo_start() immediately queues a burst of its
	 * own -- without this, one of them was the one getting dropped. */
	k_msleep(5);
}

#if DT_HAS_CHOSEN(zephyr_display)
static void display_demo(void)
{
	const struct device *disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	if (!device_is_ready(disp)) {
		printk("Display device not ready\n");
		return;
	}

	struct display_capabilities caps;
	display_get_capabilities(disp, &caps);
	printk("Display: %ux%u, pixel format 0x%x\n",
	       caps.x_resolution, caps.y_resolution, caps.current_pixel_format);

	static uint8_t buf[400 / 8 * 240];
	for (int row = 0; row < 240; row++) {
		memset(&buf[row * (400 / 8)], (row & 1) ? 0x00 : 0xFF, 400 / 8);
	}

	struct display_buffer_descriptor desc = {
		.buf_size = sizeof(buf),
		.width = 400,
		.height = 240,
		.pitch = 400,
	};

	int err = display_write(disp, 0, 0, &desc, buf);
	printk("display_write() -> %d\n", err);
}
#else
static void display_demo(void)
{
	printk("No zephyr,display chosen for this board\n");
}
#endif

int main(void)
{
	stc3100_power_latch_hold();

	printk("=== stravaV11 Phase 2/3 DK bring-up ===\n");

	led_button_demo();
	i2c_demo();
	sensor_demo("bme280", DEVICE_DT_GET(DT_NODELABEL(bme280)));
	sensor_demo("fxos8700", DEVICE_DT_GET(DT_NODELABEL(fxos8700)));
	fram_demo();
	storage_demo();
	display_demo();
	gfx_demo();
	uart_demo();
	ant_demo_start();
	hrm_demo_start();
	bsc_demo_start();
	sensor_screen_demo_start();
	ble_demo_start();
	task_demo_start();
	power_demo_start();
	poll_demo_start();
	usb_demo_start();
	gps_sim_demo_start();

	printk("=== bring-up smoke test done ===\n");
	return 0;
}
