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

#if defined(STRAVA_SEGMENTS_ENABLED)
#include "Segment.h"
#endif
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
#include "VueGPS.h"
#include "VueFEC.h"
#include "VuePRC.h"
#include "VueCRS.h"
#include "Parcours.h"
#include "att_global.h"
#if defined(STRAVA_SEGMENTS_ENABLED)
#include "SegmentManager.h"
#endif
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
#if defined(STRAVA_SEGMENTS_ENABLED)
extern SegmentManager segMngr;
#endif

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

/* GFX port Phase D: cadran()/cadranH()/cadranRR()/Histo()/HistoH() are
 * pure virtual on VueDebug/VueGPS/VueFEC/VuePRC (meant to be implemented
 * by Vue itself, ported last per the plan's own ordering -- see todo.md).
 * This minimal concrete class combines VueDebug+VueGPS+VueFEC+VuePRC (all
 * four virtually inherit the same Adafruit_GFX base, so this is a real,
 * if partial, preview of how the eventual Vue class assembles them) and
 * implements their shared pure virtuals ONCE as placeholders -- just
 * enough to prove each screen's own tasksXxx()/displayXxx() logic
 * reaches and calls them with sane arguments, not a preview of the real
 * rendering Vue::cadran() etc. will do.
 *
 * VueGPS is inherited virtually here even though VueDebug/VueFEC aren't
 * (a real, non-obvious diamond gotcha, hit while adding VuePRC, not
 * something copy-pasted from a template): VuePRC.h itself already
 * declares `virtual public VueGPS` (stravaV10's own original, since
 * VuePRC needs VueGPS's displayGPS() for its own eVuePRCScreenGps mode),
 * so once VuePRC joined this base list, a *non*-virtual VueGPS base here
 * created two separate VueGPS subobjects -- one direct, one via VuePRC --
 * making every VueGPS member (displayGPS() first) genuinely ambiguous at
 * compile time. Declaring the direct base `virtual` too merges it back
 * into VuePRC's own virtual VueGPS subobject, restoring a single shared
 * instance -- the same fix stravaV10's real `Vue` class would need if it
 * ever combined VuePRC with a directly-named VueGPS base the same way.
 *
 * VueCRS also virtually inherits VueGPS (for its own eVueCRSScreenInit
 * fallback to displayGPS()), so it slots into the already-virtual VueGPS
 * base above with no further change needed there. It does introduce a
 * *different* diamond wrinkle: VueCRS declares its own protected
 * `afficheSegment(uint8_t, Segment*)` -- same name, same signature as
 * VuePRC's own, but the two are otherwise unrelated (no shared virtual
 * base declares it once), so `this->afficheSegment(...)` from within this
 * class is genuinely ambiguous -- the compiler can't tell which one is
 * meant. The testAfficheSegment()/testCRSAfficheSegment() forwarders
 * below resolve it with explicit `VuePRC::`/`VueCRS::` qualification. */
class TestVueScreens : public VueDebug, virtual public VueGPS, public VueFEC, public VuePRC, public VueCRS {
public:
	// Adafruit_GFX is a *virtual* base shared by all three, so as the
	// most-derived class, TestVueScreens (not any one of them) is
	// responsible for initializing it directly -- each base's own
	// ": Adafruit_GFX(0, 0)" initializer is skipped once it isn't the
	// most-derived class. Real bug found the hard way: (0, 0) looked
	// harmless since drawPixel() forwards to the global `vue`'s buffer
	// regardless -- but VueFEC::tasksFEC()'s "Connecting" branch calls
	// `this->setCursor()`/`this->print()` directly (faithful to
	// stravaV10's original, where `this` and `vue` are the same object
	// once Vue itself exists), and Adafruit_GFX's own text layout clips
	// against *this* object's _width/_height before ever reaching
	// drawPixel() -- with (0, 0), every character was silently clipped,
	// not actually broken logic. Matching `vue`'s own native (unrotated)
	// dimensions here keeps this object's cursor/width/height bookkeeping
	// consistent with the buffer its drawPixel() override actually
	// writes into.
	TestVueScreens() : Adafruit_GFX(ZEPHYR_GFX_WIDTH, ZEPHYR_GFX_HEIGHT)
	{
		// Same white-on-white bug class as vue's own setTextColor(0) fix
		// in the menu test above, just on *this* object's own separate
		// textcolor member (default 1/white) instead of vue's -- found
		// the same way, by the dimension fix above not being enough on
		// its own to make VueFEC's this->print() calls visible.
		this->setTextColor(0);
	}

	// The shared virtual Adafruit_GFX base leaves drawPixel() pure
	// virtual too (Vue's own job in the real hierarchy). None of these
	// screens call it directly (everything goes through the global
	// `vue` explicitly, matching stravaV10's own code) -- this only
	// exists so TestVueScreens is concrete enough to instantiate.
	void drawPixel(int16_t x, int16_t y, uint16_t color) override
	{
		vue.drawPixel(x, y, color);
	}

	// Real bug found porting VueCRS: none of these five placeholders used
	// to reset `vue`'s cursor before printing, relying entirely on
	// println()'s own y-advance from whatever position the *previous*
	// call left it at. That's fine in isolation, but across this whole,
	// steadily-growing smoke test the cursor only ever moves down, never
	// wraps back -- by the time VueCRS::afficheScreen2() (which draws
	// exclusively through cadran()/cadranRR(), no other drawing calls of
	// its own to coincidentally reset anything) ran, `vue`'s cursor had
	// accumulated far enough below the buffer that every glyph landed
	// off-screen and got silently clipped by ZephyrGFX::drawPixel()'s own
	// bounds check -- a real "pixels 96000 -> 96000, nothing drawn"
	// result that looked exactly like a broken VueCRS::afficheScreen2(),
	// even though that function itself was already correct. A real
	// `Vue::cadran()` computes an explicit x/y from p_lig/nb_lig/p_col
	// every call rather than accumulating; these placeholders now do the
	// simplest version of the same thing -- reset to a fixed top-left
	// position before printing -- which is enough to keep them from
	// drifting off-buffer without pretending to be real layout.
	void cadranH(uint8_t p_lig, uint8_t nb_lig, const char *champ, String affi,
		     const char *p_unite) override
	{
		vue.setCursor(0, 0);
		vue.print(champ);
		vue.print(": ");
		vue.println(affi);
	}

	void cadran(uint8_t p_lig, uint8_t nb_lig, uint8_t p_col, const char *champ, String affi,
		    const char *p_unite) override
	{
		vue.setCursor(0, 0);
		vue.print(champ);
		vue.print("=");
		vue.println(affi);
	}

	void cadranRR(uint8_t p_lig, uint8_t nb_lig, uint8_t p_col, const char *champ,
		      RRZone &zone) override
	{
		vue.setCursor(0, 0);
		vue.print(champ);
		vue.println(" (RR zone placeholder)");
	}

	void Histo(uint8_t p_lig, uint8_t nb_lig, uint8_t p_col,
		   sVueHistoConfiguration &h_config_) override
	{
		vue.setCursor(0, 0);
		vue.println("(Histo placeholder)");
	}

	void HistoH(uint8_t p_lig, uint8_t nb_lig, sVueHistoConfiguration &h_config_) override
	{
		vue.setCursor(0, 0);
		vue.println("(HistoH placeholder)");
	}

	// tasksFEC() is `protected` on VueFEC (stravaV10's own original --
	// meant to be called by Vue itself, not external code). A derived
	// class can call its own base's protected members, so this thin
	// public forwarder is just for the test below to reach it.
	eVueFECScreenModes testTasksFEC()
	{
		return this->tasksFEC();
	}

	// afficheParcours()/afficheSegment() are `protected` on VuePRC (see
	// VuePRC.h's own comment on why -- widened from stravaV10's original
	// `private` so a derived test harness like this one can exercise them
	// directly with real constructed Parcours/Segment data, independently
	// of tasksPRC() -- which, per VuePRC.cpp's own top-of-file note, can
	// never actually reach them itself in this port: p_parcours is always
	// nullptr there). Same thin-forwarder pattern as testTasksFEC() above.
	void testAfficheParcours(uint8_t ligne, ListePoints2D *p_liste)
	{
		this->afficheParcours(ligne, p_liste);
	}

#if defined(STRAVA_SEGMENTS_ENABLED)
	// VuePRC:: qualification required once VueCRS (below) joins this
	// hierarchy -- see the class's own top comment on why afficheSegment()
	// alone is ambiguous without it.
	void testAfficheSegment(uint8_t ligne, Segment *p_seg)
	{
		this->VuePRC::afficheSegment(ligne, p_seg);
	}
#endif

	// Zoom's public API (increaseZoom()/decreaseZoom()/getZoomLevel()/...)
	// becomes `protected` on VuePRC via `protected Zoom` inheritance, so
	// it's likewise only reachable from within a derived class -- this
	// forwarder lets the test below observe propagateEventsPRC()'s actual
	// effect on zoom state, not just that it didn't crash.
	uint8_t testGetZoomLevel()
	{
		return this->getZoomLevel();
	}

#if defined(STRAVA_SEGMENTS_ENABLED)
	// VueCRS's own protected internals -- same thin-forwarder rationale as
	// every class above. afficheSegment() needs VueCRS:: qualification for
	// the same reason testAfficheSegment() above needs VuePRC::.
	void testCRSAfficheSegment(uint8_t ligne, Segment *p_seg)
	{
		this->VueCRS::afficheSegment(ligne, p_seg);
	}
#endif

	void testCRSAfficheScreen1()
	{
		this->afficheScreen1();
	}

	void testCRSAfficheScreen2()
	{
		this->afficheScreen2();
	}

	void testCRSAfficheSensors()
	{
		this->afficheSensors();
	}

#if defined(STRAVA_SEGMENTS_ENABLED)
	void testCRSPartner(uint8_t ligne, Segment *p_seg)
	{
		this->partner(ligne, p_seg);
	}
#endif

	// m_crs_screen_mode is `protected`, driven normally by tasksCRS()'s
	// own locator-freshness check -- which the global `locator` (never
	// given a real GPS fix in this smoke test) can never satisfy, so
	// tasksCRS() alone can never reach afficheScreen1()'s DataFull/DataSS/
	// DataDS branches. This forwarder lets the test below force a starting
	// mode directly, the same role fec_info.el_time's direct assignment
	// played for the VueFEC test above (there it's a real global; here
	// it's a protected member, hence a forwarder instead).
	void testCRSSetMode(eVueCRSScreenModes mode)
	{
		m_crs_screen_mode = mode;
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

#if defined(STRAVA_SEGMENTS_ENABLED)
	// --- Segment: build a tiny 3-point segment and check state machine getters ---
	Segment seg("test_segment");
	seg.init();
	seg.ajouterPointDebutIso(48.8566f, 2.3522f, 35.f, 0.f);
	seg.ajouterPointFin(48.8570f, 2.3530f, 36.f, 10.f);
	seg.ajouterPointFin(48.8580f, 2.3550f, 38.f, 25.f);
	printf("Segment '%s' length=%d valid=%d\n", seg.getName(), seg.longueur(), seg.isValid());
#endif

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

	// --- GFX port Phase D: VueDebug/VueGPS/VueFEC, sharing one
	// TestVueScreens instance (see its own comment above). Real
	// dependencies (locator.displayGPS2(), att.date, mes_segments, the
	// STC3100 reading, hrm_info/bsc_info/fec_info, zPower/rrZones/
	// suffer_score/powerVector) all resolve; only cadran()/cadranH()/
	// cadranRR()/Histo()/HistoH() themselves are placeholders, since
	// those belong to Vue, ported last. ---
	TestVueScreens vue_screens;

	{
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
		vue_screens.displayDebug();
		uint32_t after_debug_pixels = vue.countSetPixels();
		printf("VueDebug: displayDebug() pixels %u -> %u %s\n", before_debug_pixels,
		       after_debug_pixels,
		       after_debug_pixels < before_debug_pixels ? "drew something"
								  : "BUG: nothing drawn");
	}

	{
		vue.fillScreen(1);
		uint32_t before_gps_pixels = vue.countSetPixels();
		vue_screens.displayGPS();
		uint32_t after_gps_pixels = vue.countSetPixels();
		printf("VueGPS: displayGPS() pixels %u -> %u %s\n", before_gps_pixels, after_gps_pixels,
		       after_gps_pixels < before_gps_pixels ? "drew something" : "BUG: nothing drawn");
	}

	{
		extern sFecInfo fec_info;

		// First call: fec_info.el_time is still 0 (never set) -> Init
		// mode's own logic returns immediately without transitioning,
		// after drawing "Connecting" and queuing a notif via
		// vue.addNotif() (Phase D's NotifiableDevice addition).
		vue.fillScreen(1);
		uint32_t before_init_pixels = vue.countSetPixels();
		eVueFECScreenModes mode1 = vue_screens.testTasksFEC();
		uint32_t after_init_pixels = vue.countSetPixels();
		bool fec_init_ok =
			(mode1 == eVueFECScreenInit) && (after_init_pixels < before_init_pixels);
		printf("VueFEC: tasksFEC() while el_time=0 -> mode=%d (expect %d/Init), pixels %u -> "
		       "%u %s\n",
		       (int)mode1, (int)eVueFECScreenInit, before_init_pixels, after_init_pixels,
		       fec_init_ok ? "MATCH" : "MISMATCH");

		// Simulate FEC becoming active (matches "if (fec_info.el_time)"
		// in the original) -- this call transitions the mode but, per
		// the original's own logic, returns immediately without drawing
		// the data screen yet (that happens on the *next* call).
		fec_info.el_time = 42;
		eVueFECScreenModes mode2 = vue_screens.testTasksFEC();
		bool fec_transition_ok = (mode2 == eVueFECScreenDataFull);
		printf("VueFEC: tasksFEC() after el_time=42 -> mode=%d (expect %d/DataFull) %s\n",
		       (int)mode2, (int)eVueFECScreenDataFull,
		       fec_transition_ok ? "MATCH" : "MISMATCH");

		// Third call: now in DataFull mode -- draws the real data screen
		// (cadranH/cadran/cadranZones/cadranRR/cadranPowerVector).
		vue.fillScreen(1);
		uint32_t before_data_pixels = vue.countSetPixels();
		eVueFECScreenModes mode3 = vue_screens.testTasksFEC();
		uint32_t after_data_pixels = vue.countSetPixels();
		bool fec_data_ok =
			(mode3 == eVueFECScreenDataFull) && (after_data_pixels < before_data_pixels);
		printf("VueFEC: tasksFEC() DataFull render -> mode=%d (expect %d/DataFull), pixels %u "
		       "-> %u %s\n",
		       (int)mode3, (int)eVueFECScreenDataFull, before_data_pixels, after_data_pixels,
		       fec_data_ok ? "MATCH" : "MISMATCH");

		fec_info.el_time = 0; // leave global state clean for anything running after this
	}

	// --- GFX port Phase D: VuePRC, same TestVueScreens instance. Real
	// dependencies (locator.getLastUpdateAge(), att, segMngr,
	// hrm_info/bsc_info, the STC3100 reading) all resolve; tasksPRC()
	// itself is a deliberate trim (p_parcours is always nullptr -- see
	// VuePRC.cpp's own top-of-file note for why), so this test also
	// directly exercises afficheParcours()/afficheSegment() with real
	// constructed Parcours/Segment data via the testAffiche*() forwarders,
	// which tasksPRC() can never reach on its own in this port. ---
	{
		// A real 4-point Parcours (ajouterPointFin() takes lat/lon/alt,
		// same API Segment's own ajouterPointFin() uses, just on
		// Point2D/ListePoints2D instead of Point/ListePoints). Point
		// spacing (~5-6m, real 1Hz-cycling-speed scale) is deliberately
		// tight, not the ~50-250m spacing an earlier attempt used: real
		// bug found running this test the first time -- Zoom::computeZoom()
		// at the default zoom level (BASE_ZOOM_LEVEL=10) and this
		// screen-quadrant's own span (setSpan(400, ~68) for a 1/7-height
		// row) works out to a latitude half-span of only ~0.00038 degrees
		// (~42m) -- afficheParcours()'s/afficheSegment()'s own "only draw
		// points inside the current zoom window" filtering correctly (not
		// a code bug) drew nothing when the wider-spaced test points fell
		// entirely outside that window. Tight spacing keeps every test
		// point inside it, so the projection/filtering logic actually
		// gets exercised instead of legitimately discarding everything.
		Parcours test_parcours;
		test_parcours.ajouterPointFin(48.85740f, 2.35380f, 35.f);
		test_parcours.ajouterPointFin(48.85745f, 2.35390f, 36.f);
		test_parcours.ajouterPointFin(48.85750f, 2.35400f, 38.f);
		test_parcours.ajouterPointFin(48.85755f, 2.35410f, 40.f);

#if defined(STRAVA_SEGMENTS_ENABLED)
		// A separate, real 4-point Segment (longueur() must be >= 4 or
		// afficheSegment() bails early logging an error -- the earlier
		// `seg` test above only has 3 points, on purpose, for its own
		// isValid() check, so this is a dedicated one, not a reuse). Same
		// tight real-scale spacing as test_parcours above, same reason.
		// Real bug found running this test the first time: using
		// ajouterPointDebutIso() for the first point (matching the
		// *other* segment test's own style) left this segment at only 3
		// points instead of 4, tripping afficheSegment()'s own
		// longueur()<4 guard ("Segment ... not loaded properly") and
		// silently drawing nothing -- traced to ajouterPointDebutIso()
		// itself (Segment.cpp): it calls ajouteDebut() then immediately
		// removeLast(), which nets to a NO-OP on the list when (as here)
		// it's the very first point added, since "last" and "first" are
		// the same single element at that point. Using plain
		// ajouterPointFin() for all four points sidesteps it.
		Segment test_prc_seg("prc_test_segment");
		test_prc_seg.init();
		test_prc_seg.ajouterPointFin(48.85740f, 2.35380f, 35.f, 0.f);
		test_prc_seg.ajouterPointFin(48.85745f, 2.35390f, 36.f, 10.f);
		test_prc_seg.ajouterPointFin(48.85750f, 2.35400f, 38.f, 20.f);
		test_prc_seg.ajouterPointFin(48.85755f, 2.35410f, 40.f, 30.f);
#endif

		// Put "our position" inside both test tracks' own coordinate
		// span, matching how a real ride would have att.loc sit near the
		// route it's displaying.
		att.loc.lat = 48.85748f;
		att.loc.lon = 2.35395f;

		vue.fillScreen(1);
		uint32_t before_parcours_pixels = vue.countSetPixels();
		vue_screens.testAfficheParcours(5, test_parcours.getListePoints());
		uint32_t after_parcours_pixels = vue.countSetPixels();
		printf("VuePRC: afficheParcours() pixels %u -> %u %s\n", before_parcours_pixels,
		       after_parcours_pixels,
		       after_parcours_pixels < before_parcours_pixels ? "drew something"
									: "BUG: nothing drawn");

#if defined(STRAVA_SEGMENTS_ENABLED)
		vue.fillScreen(1);
		uint32_t before_seg_pixels = vue.countSetPixels();
		vue_screens.testAfficheSegment(5, &test_prc_seg);
		uint32_t after_seg_pixels = vue.countSetPixels();
		printf("VuePRC: afficheSegment() pixels %u -> %u %s\n", before_seg_pixels,
		       after_seg_pixels,
		       after_seg_pixels < before_seg_pixels ? "drew something" : "BUG: nothing drawn");
#endif

		vue.fillScreen(1);
		uint32_t before_loading_pixels = vue.countSetPixels();
		vue_screens.displayLoading();
		uint32_t after_loading_pixels = vue.countSetPixels();
		printf("VuePRC: displayLoading() pixels %u -> %u %s\n", before_loading_pixels,
		       after_loading_pixels,
		       after_loading_pixels < before_loading_pixels ? "drew something"
								      : "BUG: nothing drawn");

		uint8_t zoom_before = vue_screens.testGetZoomLevel();
		vue_screens.propagateEventsPRC(eButtonsEventRight); // increaseZoom()
		uint8_t zoom_after_inc = vue_screens.testGetZoomLevel();
		vue_screens.propagateEventsPRC(eButtonsEventLeft); // decreaseZoom()
		uint8_t zoom_after_dec = vue_screens.testGetZoomLevel();
		bool zoom_ok = (zoom_after_inc != zoom_before) && (zoom_after_dec == zoom_before);
		printf("VuePRC: propagateEventsPRC() zoom %u -> Right -> %u -> Left -> %u %s\n",
		       zoom_before, zoom_after_inc, zoom_after_dec, zoom_ok ? "MATCH" : "MISMATCH");

		// tasksPRC() itself: m_prc_screen_mode starts at eVuePRCScreenInit
		// (the constructor's own default), but -- per VuePRC.cpp's own
		// top-of-file note -- the very first call always overwrites it to
		// eVuePRCScreenDataFull *before* the switch below reads it (the
		// original's own pre-existing logic, not a porting artifact: the
		// `eVuePRCScreenInit != m_prc_screen_mode && ...` guard is false
		// on the first call precisely because the mode is still Init at
		// that point, so the else branch always fires). With p_parcours
		// forced nullptr, this exercises the "No PRC in memory" path plus
		// the always-drawn Avg/SOC cadran() calls below it.
		vue.fillScreen(1);
		uint32_t before_prc_pixels = vue.countSetPixels();
		eVuePRCScreenModes prc_mode = vue_screens.tasksPRC();
		uint32_t after_prc_pixels = vue.countSetPixels();
		bool prc_ok = (prc_mode == eVuePRCScreenDataFull) && (after_prc_pixels < before_prc_pixels);
		printf("VuePRC: tasksPRC() first call -> mode=%d (expect %d/DataFull), pixels %u -> %u "
		       "%s\n",
		       (int)prc_mode, (int)eVuePRCScreenDataFull, before_prc_pixels, after_prc_pixels,
		       prc_ok ? "MATCH" : "MISMATCH");
	}

	// --- GFX port Phase D: VueCRS, same TestVueScreens instance. Real
	// dependencies (locator.getLastUpdateAge(), att, segMngr, hrm_info/
	// bsc_info, rrZones, suffer_score, m_komoot_nav, the STC3100 reading)
	// all resolve. afficheSensors() is a deliberate trim (FXOS not
	// ported -- see VueCRS.cpp's own top-of-file note), so it's tested as
	// the documented placeholder it now is, not real sensor output. ---
	{
		// tasksCRS() itself, via its real entry point: the global
		// `locator` never receives a GPS fix in this smoke test, so
		// getLastUpdateAge() is always stale -- tasksCRS() can only ever
		// genuinely reach eVueCRSScreenInit this way, exercising the
		// `this->displayGPS()` substitution (see VueCRS.cpp's own note on
		// why it's `this->`, not `vue.`, in this port).
		vue.fillScreen(1);
		uint32_t before_init_pixels = vue.countSetPixels();
		eVueCRSScreenModes crs_mode = vue_screens.tasksCRS();
		uint32_t after_init_pixels = vue.countSetPixels();
		bool crs_init_ok =
			(crs_mode == eVueCRSScreenInit) && (after_init_pixels < before_init_pixels);
		printf("VueCRS: tasksCRS() (stale locator) -> mode=%d (expect %d/Init), pixels %u -> "
		       "%u %s\n",
		       (int)crs_mode, (int)eVueCRSScreenInit, before_init_pixels, after_init_pixels,
		       crs_init_ok ? "MATCH" : "MISMATCH");

#if defined(STRAVA_SEGMENTS_ENABLED)
		// afficheScreen1()'s DataFull/DataSS/DataDS branches, forced via
		// testCRSSetMode() since tasksCRS() alone can't reach them here
		// (see that forwarder's own comment). Real 4-point Segments, same
		// tight real-scale spacing established validating VuePRC, each
		// added to the real global `segMngr` (SegmentManager::addSegment()
		// stores a pointer to what's passed in, so these must outlive
		// this whole block -- they do, same scope).
		Segment test_crs_seg1("crs_test_segment_1");
		test_crs_seg1.init();
		test_crs_seg1.ajouterPointFin(48.85740f, 2.35380f, 35.f, 0.f);
		test_crs_seg1.ajouterPointFin(48.85745f, 2.35390f, 36.f, 10.f);
		test_crs_seg1.ajouterPointFin(48.85750f, 2.35400f, 38.f, 20.f);
		test_crs_seg1.ajouterPointFin(48.85755f, 2.35410f, 40.f, 30.f);

		Segment test_crs_seg2("crs_test_segment_2");
		test_crs_seg2.init();
		test_crs_seg2.ajouterPointFin(48.85760f, 2.35420f, 41.f, 0.f);
		test_crs_seg2.ajouterPointFin(48.85765f, 2.35430f, 42.f, 10.f);
		test_crs_seg2.ajouterPointFin(48.85770f, 2.35440f, 43.f, 20.f);
		test_crs_seg2.ajouterPointFin(48.85775f, 2.35450f, 44.f, 30.f);

		att.loc.lat = 48.85748f;
		att.loc.lon = 2.35395f;

		// 0 segments -> DataFull
		vue.fillScreen(1);
		uint32_t before_full_pixels = vue.countSetPixels();
		vue_screens.testCRSSetMode(eVueCRSScreenDataFull);
		vue_screens.testCRSAfficheScreen1();
		uint32_t after_full_pixels = vue.countSetPixels();
		printf("VueCRS: afficheScreen1() 0 segs -> DataFull, pixels %u -> %u %s\n",
		       before_full_pixels, after_full_pixels,
		       after_full_pixels < before_full_pixels ? "drew something"
								: "BUG: nothing drawn");

		// 1 segment -> DataSS (exercises afficheSegment() + cadranH()'s
		// "Next" fallback together, via the real integration path, not a
		// direct forwarder call)
		segMngr.addSegment(test_crs_seg1);
		vue.fillScreen(1);
		uint32_t before_ss_pixels = vue.countSetPixels();
		vue_screens.testCRSSetMode(eVueCRSScreenDataFull); // recomputed from getNbSegs() inside
		vue_screens.testCRSAfficheScreen1();
		uint32_t after_ss_pixels = vue.countSetPixels();
		printf("VueCRS: afficheScreen1() 1 seg -> DataSS, pixels %u -> %u %s\n",
		       before_ss_pixels, after_ss_pixels,
		       after_ss_pixels < before_ss_pixels ? "drew something" : "BUG: nothing drawn");

		// 2 segments, both SEG_OFF -> DataDS's "all segments OFF" branch
		// (exercises afficheSegment() called twice + cadranH())
		segMngr.addSegment(test_crs_seg2);
		vue.fillScreen(1);
		uint32_t before_ds_pixels = vue.countSetPixels();
		vue_screens.testCRSSetMode(eVueCRSScreenDataFull);
		vue_screens.testCRSAfficheScreen1();
		uint32_t after_ds_pixels = vue.countSetPixels();
		printf("VueCRS: afficheScreen1() 2 segs -> DataDS, pixels %u -> %u %s\n",
		       before_ds_pixels, after_ds_pixels,
		       after_ds_pixels < before_ds_pixels ? "drew something" : "BUG: nothing drawn");

		segMngr.clearSegs(); // leave global state clean, matching precedent

		// afficheSegment()/partner() directly, same rigor as VuePRC's own
		// direct forwarder tests, isolating each from the afficheScreen1()
		// integration above.
		vue.fillScreen(1);
		uint32_t before_crsseg_pixels = vue.countSetPixels();
		vue_screens.testCRSAfficheSegment(5, &test_crs_seg1);
		uint32_t after_crsseg_pixels = vue.countSetPixels();
		printf("VueCRS: afficheSegment() pixels %u -> %u %s\n", before_crsseg_pixels,
		       after_crsseg_pixels,
		       after_crsseg_pixels < before_crsseg_pixels ? "drew something"
								    : "BUG: nothing drawn");

		vue.fillScreen(1);
		uint32_t before_partner_pixels = vue.countSetPixels();
		vue_screens.testCRSPartner(5, &test_crs_seg1);
		uint32_t after_partner_pixels = vue.countSetPixels();
		printf("VueCRS: partner() pixels %u -> %u %s\n", before_partner_pixels,
		       after_partner_pixels,
		       after_partner_pixels < before_partner_pixels ? "drew something"
								      : "BUG: nothing drawn");
#endif /* STRAVA_SEGMENTS_ENABLED */

		// afficheScreen2() -- komoot icon lookup + cadranRR(), real
		// `m_komoot_nav`/`rrZones` globals (both zero-initialized/empty,
		// same "no ride data yet" honesty as everything else this port
		// hasn't wired a real data source into yet).
		vue.fillScreen(1);
		uint32_t before_screen2_pixels = vue.countSetPixels();
		vue_screens.testCRSAfficheScreen2();
		uint32_t after_screen2_pixels = vue.countSetPixels();
		printf("VueCRS: afficheScreen2() pixels %u -> %u %s\n", before_screen2_pixels,
		       after_screen2_pixels,
		       after_screen2_pixels < before_screen2_pixels ? "drew something"
								      : "BUG: nothing drawn");

		// afficheSensors() -- the documented FXOS-not-ported placeholder.
		vue.fillScreen(1);
		uint32_t before_sensors_pixels = vue.countSetPixels();
		vue_screens.testCRSAfficheSensors();
		uint32_t after_sensors_pixels = vue.countSetPixels();
		printf("VueCRS: afficheSensors() placeholder pixels %u -> %u %s\n",
		       before_sensors_pixels, after_sensors_pixels,
		       after_sensors_pixels < before_sensors_pixels ? "drew something"
								      : "BUG: nothing drawn");

		// propagateEventsCRS(): cycles m_screen_page Page1->Page2->Page3->
		// Page1 (Right) and the reverse (Left) -- no external getter for
		// m_screen_page exists (nothing needs one outside this test), so
		// this just confirms the real entry point runs cleanly under both
		// directions without crashing; page-selection *effect* is already
		// covered by the direct afficheScreen1()/2()/Sensors() calls
		// above.
		bool right_ok = vue_screens.propagateEventsCRS(eButtonsEventRight);
		bool left_ok = vue_screens.propagateEventsCRS(eButtonsEventLeft);
		printf("VueCRS: propagateEventsCRS() Right=%d Left=%d (expect 1/1) %s\n", right_ok,
		       left_ok, (right_ok && left_ok) ? "MATCH" : "MISMATCH");
	}

	// --- GFX port Phase D: Vue itself -- the real, fully-assembled class
	// (VueCRS+VueFEC+VuePRC+VueDebug+NotifiableDevice+Menuable+ZephyrGFX),
	// now what `vue`'s global type actually is (see vue_global.h). Tests
	// here exercise Vue's *own* methods (cadran()/cadranH()/cadranRR()/
	// Histo()/HistoH()/refresh()/tasks()/clearDisplay()/invertDisplay()) --
	// everything upstream of this point already validated each screen's
	// own tasksXxx()/displayXxx() logic via the separate TestVueScreens
	// placeholder harness; this is the first point in this port where
	// real cadran() etc. rendering (not TestVueScreens' "just enough to
	// prove reach" stand-ins) actually runs.
	//
	// Deliberately NOT calling vue.init() (or vue.initMenu() alone) here,
	// and deliberately never sending eButtonsEventCenter to vue.tasks() --
	// per the user's explicit direction not to wire buttons in this
	// increment, and because of a second, real reentrancy hazard found
	// while checking whether it would even be safe to: menu_content.cpp's
	// Menuable::initMenu() builds its menu tree against *file-scope
	// static* MenuPageItems/MenuPageSetting objects (`m_root_page`,
	// `page_value`), constructed once and bound to a specific Menuable
	// instance at static-init time (the existing `menu` global, from the
	// earlier menu test) -- not per-call state the way `this->p_root_page`
	// is. Calling `vue.initMenu()` would rebind those same static objects
	// to `vue` instead, corrupting `menu`'s already-validated tree out
	// from under it (the same class of single-owner assumption
	// `Locator::init()`'s file-scope satellite-tracking arrays turned out
	// to have, found earlier in this port). Left as a known, real
	// constraint on `Menuable::initMenu()` (only ever call it on one
	// Menuable instance per process), not chased further here since a
	// second real menu instance isn't this port's actual end state anyway
	// (there will only ever be one `vue` on real hardware). Because
	// `vue.init()` never runs, `vue.tasks()` below is exercised with
	// Left/Right only -- events that flow through `propagateEventsCRS()`/
	// `Menuable::propagateEvent()` without ever touching `p_cur_page`
	// (still null, since closeMenu()/initMenu() never ran on `vue`) --
	// sending Center would dereference that null pointer.
	{
		printf("Vue: getLastRefreshed() before first refresh() = %u (expect 0)\n",
		       vue.getLastRefreshed());

		// Default mode is VUE_DEFAULT_MODE (eVueGlobalScreenCRS,
		// parameters.h) -- refresh() should dispatch to tasksCRS() and
		// draw for real via Vue's own cadran()/cadranH(), not a
		// placeholder. att/segMngr/hrm_info/bsc_info are all real,
		// already-validated globals from the VueCRS test block above
		// (segMngr left empty by that block's own cleanup).
		vue.fillScreen(1);
		uint32_t before_crs_pixels = vue.countSetPixels();
		vue.refresh();
		uint32_t after_crs_pixels = vue.countSetPixels();
		bool crs_refresh_ok =
			(after_crs_pixels < before_crs_pixels) && (vue.getLastRefreshed() != 0);
		printf("Vue: refresh() default mode (CRS) -> pixels %u -> %u, "
		       "getLastRefreshed()=%u (expect nonzero) %s\n",
		       before_crs_pixels, after_crs_pixels, vue.getLastRefreshed(),
		       crs_refresh_ok ? "MATCH" : "MISMATCH");

		// FEC mode: fec_info.el_time is 0 (left clean by the earlier
		// VueFEC test block), so this hits VueFEC::tasksFEC()'s Init
		// branch -- which calls the real vue.addNotif("FEC",
		// "Connecting...", ...) (not a placeholder; VueFEC.cpp calls it
		// on the global `vue`, which *is* `this` here) -- so this same
		// refresh() call also exercises refresh()'s own notification-
		// banner rendering path (getTextBounds() + the text.length()>2
		// fix) in one integrated pass, not a separately-staged test.
		vue.setCurrentMode(eVueGlobalScreenFEC);
		vue.fillScreen(1);
		uint32_t before_fec_pixels = vue.countSetPixels();
		vue.refresh();
		uint32_t after_fec_pixels = vue.countSetPixels();
		printf("Vue: refresh() FEC mode (Init + notif banner) -> pixels %u -> %u %s\n",
		       before_fec_pixels, after_fec_pixels,
		       after_fec_pixels < before_fec_pixels ? "drew something"
							     : "BUG: nothing drawn");

		// PRC mode: p_parcours is always null in this port's VuePRC (see
		// VuePRC.cpp's own note), so this exercises tasksPRC()'s "No PRC
		// in memory" + always-drawn Avg/SOC cadran() path, same as the
		// VuePRC test block above but now through the real assembled
		// object and real refresh() dispatch instead of a direct call.
		vue.setCurrentMode(eVueGlobalScreenPRC);
		vue.fillScreen(1);
		uint32_t before_prc_pixels = vue.countSetPixels();
		vue.refresh();
		uint32_t after_prc_pixels = vue.countSetPixels();
		printf("Vue: refresh() PRC mode -> pixels %u -> %u %s\n", before_prc_pixels,
		       after_prc_pixels,
		       after_prc_pixels < before_prc_pixels ? "drew something"
							     : "BUG: nothing drawn");

		// DEBUG mode: displayDebug() -> locator.displayGPS2() + real
		// cadranH()/cadran() calls.
		vue.setCurrentMode(eVueGlobalScreenDEBUG);
		vue.fillScreen(1);
		uint32_t before_debug_pixels = vue.countSetPixels();
		vue.refresh();
		uint32_t after_debug_pixels = vue.countSetPixels();
		printf("Vue: refresh() DEBUG mode -> pixels %u -> %u %s\n", before_debug_pixels,
		       after_debug_pixels,
		       after_debug_pixels < before_debug_pixels ? "drew something"
								 : "BUG: nothing drawn");

		// tasks(): back to CRS mode, Left/Right only (see this block's own
		// top comment on why Center is never sent). Just confirms the
		// real dispatch path (propagateEventsCRS() + Menuable::
		// propagateEvent()) runs cleanly end-to-end with no crash --
		// propagateEventsCRS()'s own *effect* (m_screen_page cycling) is
		// already covered directly by the VueCRS test block above.
		vue.setCurrentMode(eVueGlobalScreenCRS);
		vue.tasks(eButtonsEventRight);
		vue.tasks(eButtonsEventLeft);
		printf("Vue: tasks(Right)/tasks(Left) (CRS mode, no menu) ran with no crash\n");

		// clearDisplay()/invertDisplay(): clearDisplay() -> fillScreen(1)
		// (all white, i.e. all bits set given this port's 1=white
		// convention); invertDisplay() XORs the whole buffer, so
		// immediately after clearDisplay() it should flip every bit to 0
		// (all black) -- a fully deterministic check, not just "some
		// pixels changed".
		vue.clearDisplay();
		uint32_t after_clear_pixels = vue.countSetPixels();
		vue.invertDisplay();
		uint32_t after_invert_pixels = vue.countSetPixels();
		bool invert_ok = (after_clear_pixels == ZEPHYR_GFX_WIDTH * ZEPHYR_GFX_HEIGHT) &&
				 (after_invert_pixels == 0);
		printf("Vue: clearDisplay() -> %u pixels set (expect %u), invertDisplay() -> %u "
		       "pixels set (expect 0) %s\n",
		       after_clear_pixels, ZEPHYR_GFX_WIDTH * ZEPHYR_GFX_HEIGHT, after_invert_pixels,
		       invert_ok ? "MATCH" : "MISMATCH");
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
