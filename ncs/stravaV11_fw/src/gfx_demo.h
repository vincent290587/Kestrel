#ifndef GFX_DEMO_H_
#define GFX_DEMO_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void gfx_demo(void);

/* Redraws the screen with live ANT+ HRM/BSC values (Phase 11), GPS
 * altitude, and STC3100 battery percentage -- replaces whatever
 * gfx_demo() last drew. Values are shown as "--" when their
 * *_paired/has_alt/has_batt flag is false (searching/no fix/no reading
 * yet, not stale-but-labeled-live data). */
void gfx_demo_show_sensors(uint8_t bpm, uint16_t rr_ms, bool hrm_paired, uint32_t speed_kph,
			    uint32_t cadence_rpm, bool bsc_paired, float alt_m, bool has_alt,
			    float batt_percent, bool has_batt);

/* Renders a parsed map_tile.c-format buffer (e.g. loaded from the SD
 * card by map_demo.c) centered at (center_lat, center_lon), at the fixed
 * constant zoom in map_render.h (MAP_RENDER_ZOOM_LEVEL -- maps-
 * feasibility phased plan step 4/5, see docs/maps_feasibility.md).
 * Replaces whatever was last drawn, same as gfx_demo_show_sensors().
 * Returns the number of points actually drawn (see map_render_tile()). */
uint32_t gfx_demo_show_map(const uint8_t *tile_buf, size_t tile_len, float center_lat,
			    float center_lon);

/* GFX port Phase D (display arbitration): pushes an already-rendered
 * buffer (ZEPHYR_GFX_WIDTH x ZEPHYR_GFX_HEIGHT, ZephyrGFX's own layout --
 * see that class's header) to the real display, reusing the same device
 * handle/readiness check as gfx_demo()/gfx_demo_show_sensors()/
 * gfx_demo_show_map() above. For a caller that owns its own
 * ZephyrGFX-compatible buffer directly (vue_demo.cpp's `vue`, which
 * inherits ZephyrGFX) rather than drawing into this file's own private
 * `gfx` object. Returns false if the display device isn't ready (nothing
 * pushed); true means display_write() was attempted -- check the log for
 * its actual return code, same error-tolerant convention as every other
 * function in this file. */
bool gfx_demo_push_buffer(const uint8_t *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* GFX_DEMO_H_ */
