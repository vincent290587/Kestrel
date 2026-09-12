/*
 * VueLap.cpp
 *
 * Manual-lap feature (2026-09-12). Row 1: the current (still-open) lap's
 * number and running time. Row 2: its running average power / Normalized
 * Power so far, side by side. Remaining rows: the last few *closed* laps
 * (lap_screen_get_recent_laps(), FRAM's own small display window --
 * ride_recorder.c has the full history durably in the QSPI record
 * stream regardless), most recent first.
 */

#include <VueLap.h>
#include "Screenutils.h"
#include "lap_screen_data.h"

#define VUE_LAP_NB_LINES 7

VueLap::VueLap() : Adafruit_GFX(0, 0)
{
}

void VueLap::displayLap()
{
	uint32_t elapsed_s = 0, lap_number = 0;
	uint16_t avg_power_w = 0, np_w = 0;
	bool have_current = lap_screen_get_current_lap(&elapsed_s, &avg_power_w, &np_w, &lap_number);

	String champ_current = have_current ? (String("Lap ") + String((unsigned)lap_number)) : "Lap";

	this->cadranH(1, VUE_LAP_NB_LINES, champ_current.c_str(),
		      have_current ? _secjmkstr(elapsed_s, ':') : "--:--:--", NULL);
	this->cadran(2, VUE_LAP_NB_LINES, 1, "Pwr", have_current ? _imkstr((int)avg_power_w) : "--", "W");
	this->cadran(2, VUE_LAP_NB_LINES, 2, "NP", have_current ? _imkstr((int)np_w) : "--", "W");

	struct lap_screen_lap_info recent[LAP_SCREEN_MAX_RECENT_LAPS];
	uint8_t count = lap_screen_get_recent_laps(recent);
	uint8_t max_rows = VUE_LAP_NB_LINES - 2;

	for (uint8_t i = 0; i < count && i < max_rows; i++) {
		String champ = String("L") + String((unsigned)recent[i].lap_number);
		String power_unit = String((unsigned)recent[i].avg_power_w) + "W";

		this->cadranH((uint8_t)(3 + i), VUE_LAP_NB_LINES, champ.c_str(),
			      _secjmkstr(recent[i].elapsed_s, ':'), power_unit.c_str());
	}
}
