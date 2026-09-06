/*
 * Phase 1/6 smoke test: exercises the hardware-agnostic slice of stravaV10's
 * business logic (routes/geometry, power/HR zone binning, the order-1
 * filter, the Komoot icon lookup, and now GPS/NMEA decode via TinyGPS++ +
 * Locator) ported to build under Zephyr.
 *
 * This is deliberately not a ztest suite yet -- the goal here is proving the
 * code builds and runs correctly on native_sim before investing in a real
 * test harness. Model.cpp, Boucle*, Attitude/UserSettings' FRAM persistence,
 * and GPSMGMT's real UART/hardware layer are not part of this slice --
 * they're coupled to hardware/connectivity that hasn't been ported yet (see
 * CLAUDE.md).
 */

#include <cstdio>
#include <cstring>

#include "Segment.h"
#include "PowerZone.h"
#include "SufferScore.h"
#include "order1_filter.h"
#include "komoot_nav.h"
#include "UserSettings.h"
#include "Locator.h"

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

	printf("=== smoke test done ===\n");
	return 0;
}
