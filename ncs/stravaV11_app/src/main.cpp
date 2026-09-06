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
#include "Org_01.h"
#include "map_tile.h"
#include "test_tile_data.h"

extern UserSettings u_settings;

int main(void)
{
	printf("=== stravaV11 logic-port smoke test ===\n");

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

	printf("=== smoke test done ===\n");
	return 0;
}
