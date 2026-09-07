#ifndef DRV_WS2812_STUB_H_
#define DRV_WS2812_STUB_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Test-only accessor into drv_ws2812_stub.c's tracked state -- not part
 * of drv_ws2812.h's real API, only exists on the native_sim backend. */
uint32_t drv_ws2812_stub_get_last_color(void);

#ifdef __cplusplus
}
#endif

#endif /* DRV_WS2812_STUB_H_ */
