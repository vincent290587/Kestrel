/*
 * Trimmed API contract for stravaV10's drivers/drv_ws2812.h -- the nRF5
 * SDK/nrfx_pwm-specific pieces (sdk_config.h, DRV_WS2812_PWM_INSTANCE_NO,
 * the LED-chain-max-size knob) are dropped since the Zephyr backend
 * (stravaV11_fw/adapters/drv_ws2812.c) is devicetree-configured instead
 * -- the pin/peripheral/chain-length are fixed at build time via the
 * board's own devicetree ws2812 node, not passed in at runtime. Function
 * signatures match the original exactly so notifications.c ports over
 * unmodified. Real backend: stravaV11_fw/adapters/drv_ws2812.c (Zephyr
 * led_strip API over SPI, see the board devicetree's ws2812 node for why
 * SPI -- Zephyr has no PWM-based WS2812 driver). Host/native_sim backend:
 * stravaV11_app/adapters/drv_ws2812_stub.c (no real LED, tracks the last
 * color set -- same "TDD stub forwards to a host-visible sink" pattern
 * stravaV10's own TDD/drivers/drv_ws2812.c already used).
 */

#ifndef DRV_WS2812_H_
#define DRV_WS2812_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*drv_ws2812_refresh_callback_t)(void *p_param);

/* dout_pin is accepted for API compatibility with the original caller
 * (notifications_init(pin_num)) but ignored by both backends -- the real
 * pin is fixed by the board's devicetree ws2812 node instead. */
uint32_t drv_ws2812_init(uint8_t dout_pin);

uint32_t drv_ws2812_display(drv_ws2812_refresh_callback_t p_callback, void *p_callback_param);

uint32_t drv_ws2812_refresh(drv_ws2812_refresh_callback_t p_callback, void *p_callback_param);

bool drv_ws2812_is_refreshing(void);

void drv_ws2812_set_pixel(uint32_t pixel_no, uint32_t color);

void drv_ws2812_set_pixel_all(uint32_t color);

#ifdef __cplusplus
}
#endif

#endif /* DRV_WS2812_H_ */
