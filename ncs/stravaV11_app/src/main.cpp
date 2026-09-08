/*
 * Phase 1/6 smoke test: exercises the hardware-agnostic slice of stravaV10's
 * business logic (routes/geometry, power/HR zone binning, the order-1
 * filter, the Komoot icon lookup, and now GPS/NMEA decode via TinyGPS++ +
 * Locator) ported to build under Zephyr. Also exercises map_tile.c, new
 * (not ported) code for the maps feature -- see docs/maps_feasibility.md.
 *
 * This is deliberately not a ztest suite yet -- the goal here is proving the
 * code builds and runs correctly on native_sim before investing in a real
 * test harness. Model.cpp, Boucle*, Attitude/UserSettings' FRAM persistence,
 * and GPSMGMT's real UART/hardware layer are not part of this slice --
 * they're coupled to hardware/connectivity that hasn't been ported yet (see
 * CLAUDE.md).
 */

#include <cmath>
#include <cstdio>
#include <cstring>

#include "Segment.h"
#include "PowerZone.h"
#include "SufferScore.h"
#include "order1_filter.h"
#include "komoot_nav.h"
#include "UserSettings.h"
#include "Locator.h"
#include "ZephyrGFX.h"
#include "Screenutils.h"
#include "millis.h"
#include "menu_host.h"
#include "vue_global.h"
#include "button.h"
#include "VueDebug.h"
#include "Org_01.h"
#include "map_tile.h"
#include "test_tile_data.h"
#include "map_render.h"
#include "notifications.h"
#include "drv_ws2812_stub.h"
#include "rv32_emu.h"
#include "rv32_hostcalls.h"
#include "gui_connector.h"

#if defined(CONFIG_ARCH_POSIX)
#include <unistd.h>
#endif

extern UserSettings u_settings;

/* Host side of the rv32_emu research spike (see rv32_emu.h): the guest's
 * only way to affect anything outside its own sandboxed memory arena is
 * through these three hostcalls, which deliberately call straight into
 * this port's own real ZephyrGFX/notifications code -- not stand-ins --
 * so this proves a sandboxed guest can drive real project primitives,
 * which is the actual point of the research question. */
struct riscv_host_ctx {
	ZephyrGFX *gfx;
	uint32_t pixels_drawn;
	uint32_t led_calls;
};

static int32_t riscv_hostcall_handler(struct rv32_cpu *cpu, void *user_data, int32_t id,
				       int32_t a0, int32_t a1, int32_t a2, int32_t a3,
				       int32_t a4, int32_t a5)
{
	(void)a3;
	(void)a4;
	(void)a5;
	riscv_host_ctx *ctx = static_cast<riscv_host_ctx *>(user_data);

	switch (id) {
	case HOSTCALL_DRAW_PIXEL:
		ctx->gfx->drawPixel(a0, a1, a2);
		ctx->pixels_drawn++;
		return 0;

	case HOSTCALL_LED_SET: {
		sNeopixelOrders order;

		order.event_type = eNeoEventNotify;
		order.on_time = 5;
		order.rgb[0] = (uint8_t)((a0 >> 16) & 0xff);
		order.rgb[1] = (uint8_t)((a0 >> 8) & 0xff);
		order.rgb[2] = (uint8_t)(a0 & 0xff);
		notifications_setNotify(&order);
		ctx->led_calls++;
		return 0;
	}

	case HOSTCALL_DEBUG_PRINT: {
		uint32_t addr = (uint32_t)a0;
		uint32_t len = (uint32_t)a1;

		/* Bounds-checked independently of rv32_emu.c's own internal
		 * checks -- a host handler reading guest memory is exactly
		 * the kind of code that must never trust guest-supplied
		 * offsets/lengths without re-validating them itself. */
		if (len > cpu->mem_size || addr > cpu->mem_size - len) {
			return -1;
		}
		printf("  [guest] %.*s\n", (int)len, (const char *)&cpu->mem[addr]);
		return 0;
	}

	default:
		return -1;
	}
}

/* GFX port Phase B: cross-checks ZephyrGFX's new buffer-native
 * drawFastVLine()/drawFastHLine() (and, via Adafruit_GFX's own fillRect()
 * -> loop-of-drawFastVLine() default, fillRect() too) against a naive
 * per-pixel drawPixel() reference -- the property that actually matters
 * (the optimization produces byte-identical results to the slow path it
 * replaces), not just "didn't crash". Runs at both rotation=0 (untested
 * orientation) and rotation=3 (this port's real, validated orientation)
 * since the buffer-space span direction differs between them (see
 * ZephyrGFX.cpp's own comment). Returns true if every case matched. */
static bool test_zephyrgfx_fast_paths(uint8_t rotation)
{
	ZephyrGFX fast, ref;

	fast.setRotation(rotation);
	ref.setRotation(rotation);

	const int16_t w = fast.width();
	const int16_t h = fast.height();

	struct Rect {
		int16_t x, y, w, h;
	};

	/* Fractions of the current (rotation-dependent) screen size, plus a
	 * few fixed edge cases -- byte-aligned/non-aligned starts, spans
	 * crossing a byte boundary, single-pixel, and clipping off all four
	 * edges (negative x/y, x+w/y+h past the far edge). */
	const Rect rects[] = {
		{ 0, 0, w, h },                                 // full screen
		{ 3, 5, 13, 7 },                                 // unaligned start, multi-byte span
		{ (int16_t)(w - 5), 10, 10, 5 },                 // clipped on the right
		{ -5, (int16_t)(h / 2), 20, 3 },                 // clipped on the left
		{ (int16_t)(w / 2), -3, 4, 10 },                 // clipped on the top
		{ (int16_t)(w / 2), (int16_t)(h - 5), 4, 20 },   // clipped on the bottom
		{ 7, 7, 1, 1 },                                  // single pixel
		{ 0, 0, 1, h },                                  // full-height, byte-aligned column
		{ (int16_t)(w - 1), 0, 1, h },                   // rightmost column
	};

	bool all_match = true;

	for (const Rect &r : rects) {
		fast.fillScreen(0);
		ref.fillScreen(0);

		fast.fillRect(r.x, r.y, r.w, r.h, 1);

		for (int16_t px = r.x; px < r.x + r.w; px++) {
			for (int16_t py = r.y; py < r.y + r.h; py++) {
				ref.drawPixel(px, py, 1);
			}
		}

		bool match = memcmp(fast.getBuffer(), ref.getBuffer(), fast.getBufferSize()) == 0;

		all_match = all_match && match;

		printf("  rotation=%u rect(%d,%d,%d,%d) via fillRect: fast=%u ref=%u pixels %s\n",
		       rotation, r.x, r.y, r.w, r.h, fast.countSetPixels(), ref.countSetPixels(),
		       match ? "MATCH" : "MISMATCH");
	}

	/* Also exercise drawFastHLine()/drawFastVLine() directly (not just as
	 * fillRect()'s inner loop) -- a single-row/single-column span each. */
	fast.fillScreen(0);
	ref.fillScreen(0);
	fast.drawFastHLine(2, 3, (int16_t)(w - 4), 1);
	for (int16_t px = 2; px < w - 2; px++) {
		ref.drawPixel(px, 3, 1);
	}
	bool hline_match = memcmp(fast.getBuffer(), ref.getBuffer(), fast.getBufferSize()) == 0;
	all_match = all_match && hline_match;
	printf("  rotation=%u drawFastHLine direct: %s\n", rotation, hline_match ? "MATCH" : "MISMATCH");

	fast.fillScreen(0);
	ref.fillScreen(0);
	fast.drawFastVLine(4, 2, (int16_t)(h - 4), 1);
	for (int16_t py = 2; py < h - 2; py++) {
		ref.drawPixel(4, py, 1);
	}
	bool vline_match = memcmp(fast.getBuffer(), ref.getBuffer(), fast.getBufferSize()) == 0;
	all_match = all_match && vline_match;
	printf("  rotation=%u drawFastVLine direct: %s\n", rotation, vline_match ? "MATCH" : "MISMATCH");

	return all_match;
}

/* GFX port Phase D: VueDebug::cadran()/cadranH() are pure virtual (meant
 * to be implemented by Vue itself, ported last per the plan's own
 * ordering -- see todo.md). This minimal concrete subclass is a
 * placeholder standing in for that, just enough to prove
 * VueDebug::displayDebug() reaches and calls them with sane arguments --
 * not a preview of the real rendering Vue::cadran()/cadranH() will do. */
class TestVueDebug : public VueDebug {
public:
	// Adafruit_GFX is a *virtual* base of VueDebug, so as the
	// most-derived class, TestVueDebug (not VueDebug) is responsible for
	// initializing it directly -- VueDebug's own ": Adafruit_GFX(0, 0)"
	// initializer is skipped when VueDebug isn't the most-derived class.
	// Dimensions are irrelevant here (nothing in this test reads
	// TestVueDebug's own _width/_height; all drawing goes through the
	// global `vue`).
	TestVueDebug() : Adafruit_GFX(0, 0)
	{
	}

	// VueDebug's virtual Adafruit_GFX base leaves drawPixel() pure
	// virtual too (Vue's own job in the real hierarchy). displayDebug()
	// never calls it directly (everything goes through the global `vue`
	// explicitly, matching stravaV10's own code) -- this only exists so
	// TestVueDebug is concrete enough to instantiate for the test.
	void drawPixel(int16_t x, int16_t y, uint16_t color) override
	{
		vue.drawPixel(x, y, color);
	}

	void cadranH(uint8_t p_lig, uint8_t nb_lig, const char *champ, String affi,
		     const char *p_unite) override
	{
		vue.print(champ);
		vue.print(": ");
		vue.println(affi);
	}

	void cadran(uint8_t p_lig, uint8_t nb_lig, uint8_t p_col, const char *champ, String affi,
		    const char *p_unite) override
	{
		vue.print(champ);
		vue.print("=");
		vue.println(affi);
	}
};

int main(void)
{
	printf("=== stravaV11 logic-port smoke test ===\n");

	// --- LS027simulator.jar bridge (see gui_connector.h) -- only active
	// if the LS027_GUI env var is set, so normal automated runs of this
	// smoke test are never blocked waiting for a GUI. Called this early
	// so there's time to start the jar while it blocks on accept(). ---
	gui_connector_init();

	// --- geometry: Location/Point distance ---
	Location paris(48.8566f, 2.3522f);
	Location lyon(45.7640f, 4.8357f);
	float d = paris.dist(lyon);
	printf("Paris-Lyon great-circle distance: %.1f m (expect ~%s392 km)\n", (double)d, "~");

	// --- Segment: build a tiny 3-point segment and check state machine getters ---
	Segment seg("test_segment");
	seg.init();
	seg.ajouterPointDebutIso(48.8566f, 2.3522f, 35.f, 0.f);
	seg.ajouterPointFin(48.8570f, 2.3530f, 36.f, 10.f);
	seg.ajouterPointFin(48.8580f, 2.3550f, 38.f, 25.f);
	printf("Segment '%s' length=%d valid=%d\n", seg.getName(), seg.longueur(), seg.isValid());

	// --- PowerZone: bin a few power samples (FTP comes from u_settings, whose
	// FRAM-backed persistence is stubbed out -- see adapters/fram_stub.c -- so
	// seed it with the in-memory factory defaults instead) ---
	u_settings.resetConfig();
	PowerZone pz;
	uint32_t t = 0;
	for (uint16_t p = 100; p <= 300; p += 50) {
		pz.addPowerData(p, t);
		t += 1000;
	}
	printf("PowerZone: nb_bins=%u cur_bin=%u time_total=%u\n",
	       pz.getNbBins(), pz.getCurBin(), pz.getTimeTotal());

	// --- SufferScore: feed a rising HR ---
	SufferScore suffer;
	t = 0;
	for (int hr = 90; hr <= 170; hr += 10) {
		suffer.addHrmData(hr, t);
		t += 60000;
	}
	printf("SufferScore after ramp: %.2f\n", (double)suffer.getScore());

	// --- order1_filter: low-pass a step input ---
	order1_filterType filt;
	float coeffs[5] = { 0.2f, 0.f, 0.f, 1.f, -0.8f };
	order1_filter_init(&filt, coeffs);
	float step = 1.0f;
	float out = 0.f;
	for (int i = 0; i < 20; i++) {
		order1_filter_writeInput(&filt, &step);
		out = order1_filter_readOutput(&filt);
	}
	printf("order1_filter settled output for unit step: %.3f\n", (double)out);

	// --- komoot_nav: icon lookup doesn't crash on a couple of directions ---
	const uint8_t *icon0 = komoot_nav_get_icon(0);
	const uint8_t *icon1 = komoot_nav_get_icon(1);
	printf("komoot icons: dir0=%p dir1=%p\n", (const void *)icon0, (const void *)icon1);

	// --- Locator/TinyGPS++: feed real NMEA sentences character-by-character,
	// same as gps_encode_char() would from a live UART stream, and check the
	// decoded position comes out correctly. Sentences are TinyGPSPlus's own
	// well-known test fix (from its FullExample), not invented here.
	Locator locator;
	locator.init();

	static const char nmea[] =
		"$GPRMC,045103.000,A,3014.1984,N,09749.2872,W,0.67,161.46,171015,,,A*77\r\n"
		"$GPGGA,045104.000,3014.1985,N,09749.2873,W,1,09,1.2,211.6,M,-22.5,M,,0000*62\r\n";

	for (size_t i = 0; i < strlen(nmea); i++) {
		locator_encode_char(nmea[i]);
	}

	SLoc loc = {};
	SDate date = {};
	eLocationSource src = locator.getPosition(loc, date);
	printf("Locator source=%d lat=%.4f lon=%.4f speed=%.2f\n", (int)src, (double)loc.lat,
	       (double)loc.lon, (double)loc.speed);

	// --- map_tile: decode a real tools/osm_to_tiles.py-generated tile
	// (test_tile_data.h, not hand-crafted bytes) and check every
	// decoded value against hand-computed expectations, same rigor as
	// the NMEA test above. ---
	{
		struct map_tile_iter it;
		int rc = map_tile_iter_init(&it, test_tile_bytes, sizeof(test_tile_bytes));

		printf("map_tile: iter_init() -> %d (expect 0)\n", rc);

		struct map_tile_polyline pl;
		int got = map_tile_iter_next(&it, &pl);

		printf("map_tile: polyline road_class=%u point_count=%u (expect class=%d count=%d)\n",
		       pl.road_class, pl.point_count, TEST_TILE_EXPECTED_ROAD_CLASS,
		       TEST_TILE_EXPECTED_POINT_COUNT);

		bool points_ok = (got == 1) && (pl.point_count == TEST_TILE_EXPECTED_POINT_COUNT);

		for (uint16_t i = 0; points_ok && i < pl.point_count; i++) {
			float lat = 0.f, lon = 0.f, alt = 0.f;

			map_tile_point_at(&it, &pl, i, &lat, &lon, &alt);
			float dlat = lat - test_tile_expected_lat[i];
			float dlon = lon - test_tile_expected_lon[i];
			float dalt = alt - test_tile_expected_alt[i];

			if (fabsf(dlat) > 1e-5f || fabsf(dlon) > 1e-5f || fabsf(dalt) > 1e-3f) {
				printf("map_tile: point %u MISMATCH: got (%.6f,%.6f,%.1f) expected (%.6f,%.6f,%.1f)\n",
				       i, (double)lat, (double)lon, (double)alt,
				       (double)test_tile_expected_lat[i], (double)test_tile_expected_lon[i],
				       (double)test_tile_expected_alt[i]);
				points_ok = false;
			}
		}
		printf("map_tile: all %d points %s\n", TEST_TILE_EXPECTED_POINT_COUNT,
		       points_ok ? "MATCH" : "MISMATCH");

		int after_last = map_tile_iter_next(&it, &pl);
		printf("map_tile: iter_next() after last polyline -> %d (expect 0, clean end)\n",
		       after_last);

		// --- map_tile_name_for: same tile grid tools/osm_to_tiles.py
		// used to produce this exact test tile -- (0.05, 0.05) is
		// inside it (origin 0.04,0.04 + TILE_DEG 0.02), so this must
		// name the same file. ---
		char name[MAP_TILE_NAME_MAX];

		map_tile_name_for(0.05f, 0.05f, name);
		printf("map_tile: name_for(0.05, 0.05) = \"%s\" (expect \"tile_2_2.bin\")\n", name);

		// --- error paths: a buffer that doesn't start with the magic,
		// and one that's too short to hold even the fixed header. ---
		static const uint8_t bad_magic[24] = { 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X' };
		int bad_magic_rc = map_tile_iter_init(&it, bad_magic, sizeof(bad_magic));

		printf("map_tile: iter_init(bad magic) -> %d (expect %d)\n", bad_magic_rc,
		       MAP_TILE_ERR_BAD_MAGIC);

		int too_short_rc = map_tile_iter_init(&it, test_tile_bytes, 4);

		printf("map_tile: iter_init(4-byte buffer) -> %d (expect %d)\n", too_short_rc,
		       MAP_TILE_ERR_TOO_SHORT);
	}

	// --- map_render: project + draw the same real tool-generated test
	// tile onto its own ZephyrGFX canvas, centered on the tile's own
	// first point (0.05, 0.05) -- maps-feasibility phased plan step 4
	// (docs/maps_feasibility.md): wiring the previously-dead Zoom
	// projection math to real rendering, at a fixed constant zoom
	// (MAP_RENDER_ZOOM_LEVEL). Same "check a pixel count actually
	// changed" rigor as the ZephyrGFX test below, not just "didn't
	// crash" -- a fillScreen(1)-then-render that silently drew nothing
	// would otherwise look identical to a working one. ---
	{
		ZephyrGFX map_gfx;

		map_gfx.fillScreen(1); // white background, per map_render_tile()'s own convention
		uint32_t before = map_gfx.countSetPixels();

		uint32_t points_drawn = map_render_tile(map_gfx, test_tile_bytes,
							 sizeof(test_tile_bytes), 0.05f, 0.05f);

		uint32_t after = map_gfx.countSetPixels();

		/* fillScreen(1) makes every pixel "set" (white); drawLine(...,
		 * 0) clears pixels to black, so a working render *decreases*
		 * the set-pixel count -- the opposite direction from the
		 * black-background ZephyrGFX test below. */
		printf("map_render: %u points drawn (expect %d), pixels %u -> %u (%s)\n",
		       points_drawn, TEST_TILE_EXPECTED_POINT_COUNT, before, after,
		       (points_drawn == TEST_TILE_EXPECTED_POINT_COUNT && after < before)
			       ? "drew something"
			       : "BUG: nothing drawn or wrong point count");

		gui_connector_update_ls027(map_gfx.getBuffer(), map_gfx.getBufferSize());
#if defined(CONFIG_ARCH_POSIX)
		sleep(2); // gives the simulator window time to actually show this frame
#endif
	}

	// --- ZephyrGFX: draw text (real font rasterization) + shapes, then
	// sanity-check via pixel count -- can't see actual pixels without real
	// glass attached (same as Phase 2's display_write() checks), but this
	// proves the whole Adafruit_GFX -> our buffer pipeline runs correctly. ---
	ZephyrGFX gfx;
	gfx.fillScreen(0); // black background, so drawing in white is actually visible below
	uint32_t before = gfx.countSetPixels();

	gfx.setTextColor(1);
	gfx.setFont(&Org_01);
	gfx.setCursor(10, 30);
	gfx.print("stravaV11");
	gfx.drawRect(0, 0, 100, 50, 1);
	gfx.fillRect(120, 10, 30, 30, 1);
	gfx.drawLine(0, 100, 399, 239, 1);

	uint32_t after = gfx.countSetPixels();
	printf("ZephyrGFX: buffer=%zu bytes, pixels set %u -> %u (%s)\n", gfx.getBufferSize(), before,
	       after, after > before ? "drew something" : "BUG: nothing drawn");

	gui_connector_update_ls027(gfx.getBuffer(), gfx.getBufferSize());
#if defined(CONFIG_ARCH_POSIX)
	sleep(2); // gives the simulator window time to actually show this frame
#endif

	// --- ZephyrGFX: GFX port Phase B -- buffer-native drawFastVLine()/
	// drawFastHLine() cross-checked against a naive per-pixel reference,
	// at both rotation=0 and this port's real, validated rotation=3. ---
	{
		bool rot0_ok = test_zephyrgfx_fast_paths(0);
		bool rot3_ok = test_zephyrgfx_fast_paths(3);

		printf("ZephyrGFX fast-path cross-check: rotation=0 %s, rotation=3 %s\n",
		       rot0_ok ? "all MATCH" : "SOME MISMATCH", rot3_ok ? "all MATCH" : "SOME MISMATCH");
	}

	// --- Screenutils: GFX port Phase A -- ported unmodified except dropping
	// the dead Attitude.h include (see Screenutils.h's own comment). Zero
	// Model coupling: pure math (rotate_point/course_to) and value->String
	// formatting (_imkstr/_fmkstr/_secjmkstr/_timemkstr), built on
	// already-ported WString/millis/SDate. Cross-checked against
	// hand-computed expected values, not just "didn't crash". ---
	{
		int16_t rx = 0, ry = 0;
		rotate_point(90.f, 0, 0, 1, 0, rx, ry);
		bool rotate_ok = (rx == 0 && ry == 1);
		printf("Screenutils: rotate_point(90deg, (1,0) around origin) -> (%d,%d) (expect (0,1)) %s\n",
		       rx, ry, rotate_ok ? "MATCH" : "MISMATCH");

		float course = course_to(0.f, 0.f, 0.f, 1.f);
		bool course_ok = fabsf(course - 90.f) < 0.01f;
		printf("Screenutils: course_to((0,0) -> (0,1)) = %.2f deg (expect 90.00, due east) %s\n",
		       (double)course, course_ok ? "MATCH" : "MISMATCH");

		String si = _imkstr(42);
		bool imkstr_ok = (si == "42");
		printf("Screenutils: _imkstr(42) = \"%s\" (expect \"42\") %s\n", si.c_str(),
		       imkstr_ok ? "MATCH" : "MISMATCH");

		String sf = _fmkstr(3.14159f, 2);
		bool fmkstr_ok = (sf == "3.14");
		printf("Screenutils: _fmkstr(3.14159, 2) = \"%s\" (expect \"3.14\") %s\n", sf.c_str(),
		       fmkstr_ok ? "MATCH" : "MISMATCH");

		String st1 = _secjmkstr(3661, ':');
		bool secj_ok = (st1 == "01:01:01");
		printf("Screenutils: _secjmkstr(3661s, ':') = \"%s\" (expect \"01:01:01\") %s\n",
		       st1.c_str(), secj_ok ? "MATCH" : "MISMATCH");

		String st2 = _secjmkstr(90000, ':'); // >= 86400s -- out-of-range placeholder path
		bool secj_oor_ok = (st2 == " --:--:--");
		printf("Screenutils: _secjmkstr(90000s, ':') = \"%s\" (expect \" --:--:--\") %s\n",
		       st2.c_str(), secj_oor_ok ? "MATCH" : "MISMATCH");

		SDate now_date = {};
		now_date.secj = 0;
		now_date.timestamp = millis(); // "now", so _timemkstr's own
		                                // (millis() - timestamp) addition is ~0
		String stime = _timemkstr(now_date, ':');
		bool time_ok = stime.startsWith("00:00:0");
		printf("Screenutils: _timemkstr(secj=0, taken \"now\") = \"%s\" (expect ~\"00:00:00\") %s\n",
		       stime.c_str(), time_ok ? "MATCH" : "MISMATCH");
	}

	// --- GFX port Phase D: Menuable/MenuObjects (menu widget system) --
	// reusable class mechanics ported unmodified from stravaV10; the menu
	// tree itself (menu_content.cpp) is new for this port, not a straight
	// port, since stravaV10's real tree needs Boucle* (not ported) --
	// see menu_content.cpp's own top-of-file note. Exercises real
	// navigation state transitions and a real UserSettings round trip
	// (not stubbed), cross-checked against expected values, not just
	// "didn't crash". ---
	{
		extern int g_menu_test_pair_hrm_calls;

		menu.initMenu();

		/* Real bug found here: Adafruit_GFX's own default textcolor is 1
		 * ("white" under this port's 1=white ZephyrGFX convention -- see
		 * ZephyrGFX.h), so without this, menu text would be invisible
		 * white-on-white -- same class of bug gfx_demo.cpp's screens
		 * already had to fix (its own "switched to conventional
		 * black-ink-on-white-background" note). A future ported Vue::
		 * init() should set this once; stood in for that here since it
		 * doesn't exist yet. */
		vue.setTextColor(0);

		// Boot-time debounce: an event sent immediately after initMenu()
		// must be ignored (see Menuable::propagateEvent()'s own comment).
		menu.propagateEvent(eButtonsEventCenter);
		bool debounce_ok = !menu.m_is_menu_selected;
		printf("menu: event sent before the 5s boot debounce -> selected=%d (expect 0) %s\n",
		       menu.m_is_menu_selected, debounce_ok ? "MATCH" : "MISMATCH");

		delay_ms(5100);

		// Open the menu, then render it. vue's buffer starts all-white
		// (ZephyrGFX inits to 0xFF); drawing black (0) text/highlights
		// onto it *decreases* the set-pixel count -- same direction the
		// map_render test below already learned the hard way (its own
		// comment: "black-background ZephyrGFX test" is the one that
		// increases; this one's the opposite, white background).
		uint32_t before_menu_pixels = vue.countSetPixels();
		menu.propagateEvent(eButtonsEventCenter);
		bool opened_ok = menu.m_is_menu_selected;
		menu.tasksMenu();
		uint32_t after_menu_pixels = vue.countSetPixels();
		printf("menu: opened after debounce -> selected=%d (expect 1) %s; render pixels %u -> "
		       "%u %s\n",
		       menu.m_is_menu_selected, opened_ok ? "MATCH" : "MISMATCH", before_menu_pixels,
		       after_menu_pixels,
		       after_menu_pixels < before_menu_pixels ? "drew something" : "BUG: nothing drawn");

		// Root page items, in order: [0]=Back (auto-inserted), [1]=Pair
		// HRM, [2]=Pair BSC, [3]=Pair FEC, [4]=Set FTP, [5]=Set Weight --
		// ind_sel starts at 0, so 4x Right reaches "Set FTP".
		for (int i = 0; i < 4; i++) {
			menu.propagateEvent(eButtonsEventRight);
		}
		menu.propagateEvent(eButtonsEventCenter); // select "Set FTP"

		uint16_t ftp_before = u_settings.getFTP();

		menu.propagateEvent(eButtonsEventLeft); // decrement the setting value
		menu.propagateEvent(eButtonsEventLeft);
		menu.propagateEvent(eButtonsEventLeft);
		menu.propagateEvent(eButtonsEventCenter); // commit (post_hook -> writeConfig())

		uint16_t ftp_after = u_settings.getFTP();
		// Committing a setting returns to its parent page (the root menu),
		// it does NOT close the whole menu -- MenuPage::goToParent() only
		// calls closeMenu() when the current page has no parent at all.
		bool ftp_ok = (ftp_after == ftp_before - 3) && menu.m_is_menu_selected;
		printf("menu: Set FTP %u -> 3x Left -> commit -> %u (expect %u), menu still open=%d "
		       "(expect 1) %s\n",
		       ftp_before, ftp_after, ftp_before - 3, menu.m_is_menu_selected,
		       ftp_ok ? "MATCH" : "MISMATCH");

		// Menu is already open here, back at the root page with ind_sel
		// reset to 0 ("Back") by the goToParent() above -- select
		// "Pair HRM" ([1], one Right press) to check dispatch reaches the
		// right callback (see menu_content.cpp's test-observable call
		// counters) and that returning eFuncMenuActionEndMenu closes the
		// menu (unlike a setting page's own goToParent(), MenuItem::
		// clickAction() calls closeMenuPopagate() -> closeMenu()
		// directly on EndMenu, regardless of page nesting).
		menu.propagateEvent(eButtonsEventRight);  // -> "Pair HRM"
		menu.propagateEvent(eButtonsEventCenter); // select it

		bool pair_ok = (g_menu_test_pair_hrm_calls == 1) && !menu.m_is_menu_selected;
		printf("menu: Pair HRM selected -> callback calls=%d (expect 1), menu closed=%d %s\n",
		       g_menu_test_pair_hrm_calls, !menu.m_is_menu_selected,
		       pair_ok ? "MATCH" : "MISMATCH");
	}

	// --- GFX port Phase D: VueDebug. Its own real dependencies
	// (locator.displayGPS2(), att.date, mes_segments, the STC3100
	// current/voltage reading) all resolve; only cadran()/cadranH()
	// themselves are a placeholder (see TestVueDebug above), since those
	// belong to Vue, ported last. ---
	{
		TestVueDebug debug_screen;

		// Deliberately NOT calling locator.init() again here: it's
		// already been called once above (the earlier local `Locator
		// locator;` GPS test), and satNumber[]/elevation[]/azimuth[]/
		// snr[] (which init() registers via TinyGPSCustom::begin() ->
		// TinyGPSPlus::insertCustom()) are file-scope globals in
		// Locator.cpp, NOT per-instance state -- shared by every Locator
		// object, including this different (global) `locator`. Real bug
		// found the hard way: insertCustom() has no reentrancy guard, so
		// calling init() a second time re-inserts the same nodes into
		// gps's internal linked list, corrupting it into a cycle and
		// hanging the process. This is a genuine constraint on
		// Locator::init(), not specific to this test -- worth remembering
		// if/when something later creates another Locator and is tempted
		// to call init() on it too.
		vue.fillScreen(1); // white background, same convention as the menu test
		uint32_t before_debug_pixels = vue.countSetPixels();
		debug_screen.displayDebug();
		uint32_t after_debug_pixels = vue.countSetPixels();
		printf("VueDebug: displayDebug() pixels %u -> %u %s\n", before_debug_pixels,
		       after_debug_pixels,
		       after_debug_pixels < before_debug_pixels ? "drew something"
								  : "BUG: nothing drawn");
	}

	// --- notifications.c: port of stravaV10's WS2812 status-LED animation
	// state machine, backed here by drv_ws2812_stub.c (no real LED on
	// native_sim, just tracks the last color set). Drives a one-shot
	// "boot flash" pulse the same way main.cpp does on real hardware
	// (SET_NEO_EVENT_RED + notifications_setNotify), then ticks
	// notifications_tasks() through a full ramp-up/ramp-down cycle and
	// checks the color actually moves away from off and back again --
	// not just that it compiles and doesn't crash. ---
	notifications_init(0);
	uint32_t idle_color = drv_ws2812_stub_get_last_color();

	sNeopixelOrders boot_flash;

	SET_NEO_EVENT_RED(boot_flash, eNeoEventNotify, 0);
	notifications_setNotify(&boot_flash);

	uint32_t peak_color = 0;

	for (int i = 0; i < 15; i++) {
		notifications_tasks();
		uint32_t c = drv_ws2812_stub_get_last_color();

		if (c > peak_color) {
			peak_color = c;
		}
	}

	uint32_t settled_color = drv_ws2812_stub_get_last_color();

	printf("notifications: idle=0x%06x peak=0x%06x settled=0x%06x (%s)\n", idle_color,
	       peak_color, settled_color,
	       (idle_color == 0 && peak_color > 0 && settled_color == 0)
		       ? "pulse ramped up and back down"
		       : "BUG: pulse didn't ramp correctly");

	// --- rv32_emu: RV32IM interpreter for running untrusted "plugin" guest
	// code in a sandbox -- research spike toward eventually letting
	// third-party code call into real project primitives (pixel drawing,
	// LED notifications) without host memory access or the ability to hang
	// the caller. Guest binaries are prebuilt offline by
	// tools/riscv_guest/build.sh; run that first if these fail to open.
	// Three cases: a normal guest (real pixels/LED via the hostcall
	// handler above), a deliberate out-of-bounds guest (proves the memory
	// bounds check actually traps instead of corrupting host memory), and
	// a deliberate infinite loop (proves the instruction budget actually
	// trips instead of hanging this smoke test). ---
	{
		static uint8_t guest_mem[64 * 1024];
		ZephyrGFX riscv_gfx;

		riscv_gfx.fillScreen(0);

		riscv_host_ctx ctx = {&riscv_gfx, 0, 0};

		auto run_guest = [&](const char *bin_name) -> rv32_result {
			char path[256];

			snprintf(path, sizeof(path), "%s/%s", RISCV_GUEST_BIN_DIR, bin_name);
			FILE *f = fopen(path, "rb");

			if (!f) {
				printf("rv32_emu: %s: could not open (run "
				       "tools/riscv_guest/build.sh first)\n",
				       bin_name);
				return RV32_ERR_MEM_FAULT;
			}
			uint8_t bin[4096];
			size_t n = fread(bin, 1, sizeof(bin), f);

			fclose(f);

			rv32_cpu cpu;

			rv32_emu_reset(&cpu, guest_mem, sizeof(guest_mem));
			rv32_emu_set_hostcall_handler(&cpu, riscv_hostcall_handler, &ctx);
			if (rv32_emu_load_flat_binary(&cpu, bin, (uint32_t)n) != 0) {
				printf("rv32_emu: %s: binary too large for arena\n", bin_name);
				return RV32_ERR_MEM_FAULT;
			}

			uint32_t steps = 0;
			rv32_result r = rv32_emu_run(&cpu, 200000, &steps);
			const char *rstr;

			switch (r) {
			case RV32_HALTED_EXIT: rstr = "exited"; break;
			case RV32_ERR_MEM_FAULT: rstr = "MEM_FAULT (trapped)"; break;
			case RV32_ERR_ILLEGAL_INSN: rstr = "ILLEGAL_INSN (trapped)"; break;
			case RV32_ERR_MISALIGNED_PC: rstr = "MISALIGNED_PC (trapped)"; break;
			case RV32_ERR_NO_HOSTCALL_HANDLER: rstr = "NO_HOSTCALL_HANDLER"; break;
			case RV32_ERR_INSN_BUDGET_EXCEEDED: rstr = "BUDGET_EXCEEDED (trapped)"; break;
			default: rstr = "?"; break;
			}
			printf("rv32_emu: %-10s -> %-24s steps=%-7u exit_code=%-6d pc=0x%05x\n",
			       bin_name, rstr, steps, cpu.exit_code, cpu.pc);
			return r;
		};

		run_guest("test1.bin");
		uint32_t riscv_pixels = riscv_gfx.countSetPixels();

		/* test1.bin's HOSTCALL_LED_SET call only enqueues the order
		 * (notifications_setNotify()) -- same as the plain
		 * notifications.c section above, actually ramping the color
		 * needs notifications_tasks() ticks, so do that here too
		 * rather than just trusting led_calls==1 happened. */
		uint32_t riscv_led_peak = 0;

		for (int i = 0; i < 15; i++) {
			notifications_tasks();
			uint32_t c = drv_ws2812_stub_get_last_color();

			if (c > riscv_led_peak) {
				riscv_led_peak = c;
			}
		}

		/* notifications_tasks()'s ramp scales each channel by
		 * ratio^2/255 (see notifications.c), peaking at ratio=8 for
		 * on_time=5 -- the same shape the plain-C red pulse above
		 * peaked at 0x400000 for, just in the green channel here:
		 * 0xff * 8*8/255 = 0x40. */
		printf("rv32_emu: test1 drew %u host pixels via HOSTCALL_DRAW_PIXEL, led_calls=%u, "
		       "peak LED color after guest's led_set(0x00ff00)=0x%06x (%s)\n",
		       riscv_pixels, ctx.led_calls, riscv_led_peak,
		       (riscv_pixels == 40 && ctx.led_calls == 1 && riscv_led_peak == 0x004000)
			       ? "MATCH"
			       : "BUG: unexpected count or color");

		rv32_result oob_result = run_guest("test_oob.bin");

		printf("rv32_emu: test_oob isolation check: %s\n",
		       (oob_result == RV32_ERR_MEM_FAULT) ? "MATCH (fault trapped cleanly)"
							    : "BUG: out-of-bounds access not trapped");

		rv32_result loop_result = run_guest("test_loop.bin");

		printf("rv32_emu: test_loop isolation check: %s\n",
		       (loop_result == RV32_ERR_INSN_BUDGET_EXCEEDED)
			       ? "MATCH (instruction budget enforced)"
			       : "BUG: infinite loop not bounded");
	}

	printf("=== smoke test done ===\n");
	return 0;
}
