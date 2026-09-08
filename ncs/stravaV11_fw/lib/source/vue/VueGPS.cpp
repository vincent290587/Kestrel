/*
 * VueGPS.cpp
 *
 *  Created on: 27 f�vr. 2018
 *      Author: Vincent
 *
 * GFX port Phase D: near-identical to VueDebug.cpp (stravaV10's own
 * VueDebug.cpp was itself created by copy-pasting this file -- its
 * header comment literally still says "VueGPS.cpp"), so ported the same
 * way: replaced "#include Model.h" with the specific globals needed
 * (vue_global.h, att_global.h, Locator.h for `locator`, debug_screen_data.h
 * in place of `stc`/`mes_segments.size()`). See VueDebug.cpp's own
 * comment for why the STC/segment-count reads go through
 * debug_screen_data.h rather than a direct global.
 */

#include <VueGPS.h>
#include "Screenutils.h"
#include "Locator.h"
#include "GPSMGMT.h"
#include "vue_global.h"
#include "att_global.h"
#include "debug_screen_data.h"
#include "utils.h"

extern Locator locator;
extern GPS_MGMT gps_mgmt;

#define VUE_GPS_NB_LINES 7

VueGPS::VueGPS() : Adafruit_GFX(0, 0)
{
}

void VueGPS::displayGPS()
{
	vue.setCursor(20, 20);
	vue.setTextSize(2);

	if (!gps_mgmt.isEPOUpdating()) {
		locator.displayGPS2();
	} else {
		vue.println("EPO update in progress");
	}

	vue.println("  ----- MEM -----");

	String segs = " ";
	segs += debug_screen_get_segment_count();
	segs += " segments loaded";
	vue.println(segs);

	this->cadranH(6, VUE_GPS_NB_LINES, "Time", _timemkstr(att.date, ':'), NULL);

	float current_ma = 0.f, voltage_v = 0.f;
	bool has_stc = debug_screen_get_stc_current_ma(&current_ma);

	(void)debug_screen_get_stc_voltage_v(&voltage_v);

	this->cadran(7, VUE_GPS_NB_LINES, 1, "STC", _imkstr((int)current_ma), "mA");
	this->cadran(7, VUE_GPS_NB_LINES, 2, "SOC", has_stc ? _imkstr((int)percentageBatt(voltage_v, current_ma)) : "--", "%");
}
