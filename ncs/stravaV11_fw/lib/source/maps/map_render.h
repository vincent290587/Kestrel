/*
 * Projects and draws a parsed map_tile.c buffer onto a ZephyrGFX canvas,
 * centered at a given lat/lon -- the maps-feasibility phased plan's step
 * 4 (docs/maps_feasibility.md): wiring the previously-dead Zoom
 * projection math (source/display/Zoom.h/.cpp, ported in Phase 1, never
 * called by anything until now) to real rendering.
 *
 * Zoom is fixed, not interactive: no button/menu-driven pan/zoom exists
 * yet in this port (button.h/Notif.h are still un-ported, per Phase 7),
 * and the user explicitly asked for a constant zoom for now, set by one
 * easily-changeable value -- MAP_RENDER_ZOOM_LEVEL below. Change that
 * single define to zoom in/out; nothing else needs touching.
 *
 * Drawing every road at equal 1px weight, in file order, looked like an
 * undifferentiated tangle on real glass. map_render.cpp now filters by
 * road_class (dropping service/track and foot-only paths -- but keeping
 * cycleways, since this is a cycling computer), draws thicker lines for
 * major roads and cycleways, and draws in a fixed least-to-most-prominent
 * order so major roads/cycleways aren't broken up by whatever else the
 * tile happens to store after them. See map_render.cpp's own comments
 * for the exact class/order/weight rules.
 */

#ifndef MAP_RENDER_H_
#define MAP_RENDER_H_

#include <stddef.h>
#include <stdint.h>

#include "ZephyrGFX.h"

/* The single value to change for a different constant zoom. Same units
 * as Zoom::m_zoom_level (Zoom.h's BASE_ZOOM_LEVEL default is 10 --
 * lower means more zoomed in, per Zoom::increaseZoom()'s own comment).
 */
#define MAP_RENDER_ZOOM_LEVEL 10

/* Renders every polyline in `tile_buf` (a raw map_tile.c-format buffer,
 * e.g. as loaded from an SD card tile file) onto `gfx`, centered at
 * (center_lat, center_lon) and scaled to MAP_RENDER_ZOOM_LEVEL. Draws in
 * black (color 0) over whatever's already in the buffer -- callers
 * should fillScreen(1) first for a clean white background, same
 * convention gfx_demo_show_sensors() already uses.
 *
 * Returns the number of points actually drawn (0 on a malformed/empty
 * tile, or if map_tile_iter_init() fails), for validation -- matches
 * the ZephyrGFX smoke test's own "check a pixel count actually changed"
 * rigor rather than trusting drawLine() calls silently succeeded.
 */
uint32_t map_render_tile(ZephyrGFX &gfx, const uint8_t *tile_buf, size_t tile_len,
			  float center_lat, float center_lon);

#endif /* MAP_RENDER_H_ */
