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
#include <zephyr/sys/printk.h>

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

	uint8_t pattern[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
	int err = eeprom_write(fram, 0, pattern, sizeof(pattern));
	printk("eeprom_write() -> %d\n", err);
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
	printk("=== stravaV11 Phase 2/3 DK bring-up ===\n");

	led_button_demo();
	i2c_demo();
	sensor_demo("bme280", DEVICE_DT_GET(DT_NODELABEL(bme280)));
	sensor_demo("fxos8700", DEVICE_DT_GET(DT_NODELABEL(fxos8700)));
	fram_demo();
	display_demo();

	printk("=== bring-up smoke test done ===\n");
	return 0;
}
