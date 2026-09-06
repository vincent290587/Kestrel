#include "map_render.h"

#include "Zoom.h"

extern "C" {
#include "map_tile.h"
}

uint32_t map_render_tile(ZephyrGFX &gfx, const uint8_t *tile_buf, size_t tile_len,
			  float center_lat, float center_lon)
{
	static Zoom zoom;

	zoom.setSpan((uint16_t)gfx.width(), (uint16_t)gfx.height());
	zoom.setZoomLevel(MAP_RENDER_ZOOM_LEVEL);

	float h_zoom_deg = 0.f, v_zoom_deg = 0.f;

	/* `distance` (Zoom::computeZoom()'s 2nd argument) only feeds a
	 * commented-out "make sure the point is on screen" branch in
	 * Zoom.cpp -- dead in the live code path -- so 0 here has no
	 * effect either way. */
	zoom.computeZoom(center_lat, 0.f, h_zoom_deg, v_zoom_deg);

	if (h_zoom_deg <= 0.f || v_zoom_deg <= 0.f) {
		return 0;
	}

	struct map_tile_iter it;

	if (map_tile_iter_init(&it, tile_buf, tile_len) != MAP_TILE_OK) {
		return 0;
	}

	float w = (float)gfx.width();
	float h = (float)gfx.height();
	uint32_t points_drawn = 0;
	struct map_tile_polyline pl;
	int got;

	while ((got = map_tile_iter_next(&it, &pl)) == 1) {
		int16_t prev_x = 0, prev_y = 0;
		bool have_prev = false;

		for (uint16_t i = 0; i < pl.point_count; i++) {
			float lat = 0.f, lon = 0.f, alt = 0.f;

			map_tile_point_at(&it, &pl, i, &lat, &lon, &alt);

			/* Longitude increases eastward, same direction as
			 * screen x; latitude increases northward, opposite
			 * of screen y (which increases downward) -- hence
			 * the h/(2*h_zoom_deg) but 1-v/(2*v_zoom_deg) below. */
			float xf = (lon - center_lon + h_zoom_deg) / (2.f * h_zoom_deg) * w;
			float yf = h - (lat - center_lat + v_zoom_deg) / (2.f * v_zoom_deg) * h;
			int16_t x = (int16_t)xf;
			int16_t y = (int16_t)yf;

			if (have_prev) {
				/* Adafruit_GFX's own drawLine()/writePixel() bounds-check
				 * against the canvas already -- no clipping needed here. */
				gfx.drawLine(prev_x, prev_y, x, y, 0);
			}
			prev_x = x;
			prev_y = y;
			have_prev = true;
			points_drawn++;
		}
	}

	return points_drawn;
}
