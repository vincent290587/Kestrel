/*
 * Host/native_sim backend for drv_ws2812.h -- no real LED on native_sim,
 * so this just tracks the last color notifications.c asked for, same
 * "TDD stub forwards to a host-visible sink" pattern stravaV10's own
 * TDD/drivers/drv_ws2812.c used (there, forwarded to a TCP-socket GUI
 * visualizer; here, just a getter the smoke test can assert against --
 * no GUI visualizer exists in this port).
 */

#include <stddef.h>

#include "drv_ws2812.h"
#include "drv_ws2812_stub.h"

static uint32_t s_last_color;
static bool s_refreshing;

uint32_t drv_ws2812_init(uint8_t dout_pin)
{
	(void)dout_pin;
	s_last_color = 0;
	s_refreshing = false;
	return 0;
}

uint32_t drv_ws2812_refresh(drv_ws2812_refresh_callback_t p_callback, void *p_callback_param)
{
	if (p_callback != NULL) {
		p_callback(p_callback_param);
	}
	return 0;
}

uint32_t drv_ws2812_display(drv_ws2812_refresh_callback_t p_callback, void *p_callback_param)
{
	return drv_ws2812_refresh(p_callback, p_callback_param);
}

bool drv_ws2812_is_refreshing(void)
{
	return s_refreshing;
}

void drv_ws2812_set_pixel(uint32_t pixel_no, uint32_t color)
{
	(void)pixel_no;
	s_last_color = color;
}

void drv_ws2812_set_pixel_all(uint32_t color)
{
	s_last_color = color;
}

/* Test-only accessor -- not part of drv_ws2812.h's real API. */
uint32_t drv_ws2812_stub_get_last_color(void)
{
	return s_last_color;
}
