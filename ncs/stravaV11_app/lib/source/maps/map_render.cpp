#include "map_render.h"

#include "Zoom.h"

extern "C" {
#include "map_tile.h"
}

namespace {

/* Cohen-Sutherland-style outcode: which side(s) of the canvas a point
 * falls outside of (0 = inside). Two points sharing a set bit means the
 * segment between them cannot possibly cross the canvas (trivial
 * reject) -- e.g. both left of x=0, so the whole segment is left of it
 * too. This is what stops map_render_tile() wasting time (and, before
 * this fix, risking undefined behavior on an unclamped float->int16_t
 * cast -- see below) on the many real map points that fall well outside
 * the current view: a tile covers ~2km, the visible span at
 * MAP_RENDER_ZOOM_LEVEL is a few hundred metres. */
uint8_t outcode(float x, float y, float w, float h)
{
	uint8_t code = 0;

	if (x < 0.f) {
		code |= 0x1;
	} else if (x > w) {
		code |= 0x2;
	}
	if (y < 0.f) {
		code |= 0x4;
	} else if (y > h) {
		code |= 0x8;
	}
	return code;
}

/* Clamps to a generous margin around the canvas before casting to
 * int16_t. Real map points routinely project far outside the visible
 * area (unlike the tiny synthetic test tile used to validate this
 * function, whose points all happened to land near-center) -- an
 * unclamped cast of an extreme float to int16_t is undefined behavior,
 * not just visually wrong, and even where well-defined in practice, an
 * extreme coordinate handed to drawLine() blows up its Bresenham
 * iteration count. Found the hard way: on real hardware with real OSM
 * data (745 polylines, some points kilometres from the view center),
 * the unclamped version ran long enough on the shared system work queue
 * to disrupt MPSL radio timing and crash the device after ~2.5 minutes
 * -- the same class of "starved the radio stack" failure this project
 * already root-caused once before (Phase 11's ANT+/MPSL version-gap
 * crash), just from a different cause this time. */
int16_t clamp_coord(float v, float lo, float hi)
{
	if (v < lo) {
		return (int16_t)lo;
	}
	if (v > hi) {
		return (int16_t)hi;
	}
	return (int16_t)v;
}

} // namespace

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
	float margin_x = 4.f * w;
	float margin_y = 4.f * h;
	float x_lo = -margin_x, x_hi = w + margin_x;
	float y_lo = -margin_y, y_hi = h + margin_y;

	uint32_t points_drawn = 0;
	struct map_tile_polyline pl;
	int got;

	while ((got = map_tile_iter_next(&it, &pl)) == 1) {
		int16_t prev_x = 0, prev_y = 0;
		uint8_t prev_code = 0;
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
			uint8_t code = outcode(xf, yf, w, h);
			int16_t x = clamp_coord(xf, x_lo, x_hi);
			int16_t y = clamp_coord(yf, y_lo, y_hi);

			/* Trivial reject: both endpoints share an out-of-bounds
			 * side, so the segment between them can't cross the
			 * canvas -- Adafruit_GFX's own bounds-checked
			 * drawLine()/writePixel() would draw nothing visible for
			 * it anyway, just at the cost of walking every pixel of
			 * a potentially very long, entirely-offscreen line. */
			if (have_prev && (prev_code & code) == 0) {
				gfx.drawLine(prev_x, prev_y, x, y, 0);
			}
			prev_x = x;
			prev_y = y;
			prev_code = code;
			have_prev = true;
			points_drawn++;
		}
	}

	return points_drawn;
}
