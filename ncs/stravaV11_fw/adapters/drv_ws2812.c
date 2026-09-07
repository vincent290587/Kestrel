/*
 * Real Zephyr backend for drv_ws2812.h -- drives the board's WS2812
 * status LED (NEO_PIN, see the board devicetree's spi3/ws2812 nodes) via
 * Zephyr's led_strip API instead of stravaV10's original nrfx_pwm-direct
 * driver; Zephyr has no PWM-based WS2812 driver, worldsemi,ws2812-spi is
 * the standard/maintained approach for this chip family.
 *
 * led_strip_update_rgb() is synchronous (blocks until the SPI transfer
 * completes), unlike the original ISR-callback-driven PWM driver -- so
 * drv_ws2812_display()/refresh() just call it directly and invoke the
 * given callback (if any) immediately afterward rather than truly
 * asynchronously. drv_ws2812_is_refreshing() therefore always returns
 * false: there's no in-flight state to report.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/logging/log.h>

#include "drv_ws2812.h"

LOG_MODULE_REGISTER(drv_ws2812, LOG_LEVEL_INF);

#define WS2812_NODE DT_ALIAS(led_strip)
#define NUM_PIXELS  DT_PROP(WS2812_NODE, chain_length)

static const struct device *s_dev;
static struct led_rgb s_pixels[NUM_PIXELS];

uint32_t drv_ws2812_init(uint8_t dout_pin)
{
	ARG_UNUSED(dout_pin); /* real pin is fixed by the devicetree node, see drv_ws2812.h */

	s_dev = DEVICE_DT_GET(WS2812_NODE);

	if (!device_is_ready(s_dev)) {
		LOG_ERR("WS2812 device not ready");
		return 1;
	}

	for (size_t i = 0; i < NUM_PIXELS; i++) {
		s_pixels[i] = (struct led_rgb){0, 0, 0};
	}

	return 0;
}

uint32_t drv_ws2812_refresh(drv_ws2812_refresh_callback_t p_callback, void *p_callback_param)
{
	int err = led_strip_update_rgb(s_dev, s_pixels, NUM_PIXELS);

	if (p_callback != NULL) {
		p_callback(p_callback_param);
	}

	return (uint32_t)err;
}

uint32_t drv_ws2812_display(drv_ws2812_refresh_callback_t p_callback, void *p_callback_param)
{
	return drv_ws2812_refresh(p_callback, p_callback_param);
}

bool drv_ws2812_is_refreshing(void)
{
	return false;
}

void drv_ws2812_set_pixel(uint32_t pixel_no, uint32_t color)
{
	if (pixel_no >= NUM_PIXELS) {
		return;
	}

	s_pixels[pixel_no].r = (color >> 16) & 0xFF;
	s_pixels[pixel_no].g = (color >> 8) & 0xFF;
	s_pixels[pixel_no].b = color & 0xFF;
}

void drv_ws2812_set_pixel_all(uint32_t color)
{
	for (size_t i = 0; i < NUM_PIXELS; i++) {
		drv_ws2812_set_pixel(i, color);
	}
}
