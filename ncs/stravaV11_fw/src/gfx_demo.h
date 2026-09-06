#ifndef GFX_DEMO_H_
#define GFX_DEMO_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void gfx_demo(void);

/* Redraws the screen with live ANT+ HRM/BSC values (Phase 11) -- replaces
 * whatever gfx_demo() last drew. Values are shown as "--" when their
 * *_paired flag is false (searching, not stale-but-labeled-live data). */
void gfx_demo_show_sensors(uint8_t bpm, uint16_t rr_ms, bool hrm_paired, uint32_t speed_kph,
			    uint32_t cadence_rpm, bool bsc_paired);

#ifdef __cplusplus
}
#endif

#endif /* GFX_DEMO_H_ */
