/*
 * VueDebug.cpp
 *
 *  Created on: 27 f�vr. 2018
 *      Author: Vincent
 *
 * GFX port Phase D: replaced stravaV10's single "#include Model.h" with
 * the specific globals this file actually needs (vue_global.h,
 * att_global.h, Locator.h for the global `locator`) plus
 * debug_screen_data.h in place of the global `stc` object AND
 * mes_segments.size() (see that header's own comment for why the segment
 * count goes through it too -- ListeSegments/Segment.h was never ported
 * into stravaV11_fw, only stravaV11_app, Phase 1). Kept identical between
 * both apps' copies of this file on that basis.
 */

#include <VueDebug.h>
#include "Screenutils.h"
#include "Locator.h"
#include "vue_global.h"
#include "att_global.h"
#include "debug_screen_data.h"
#include "utils.h"

extern Locator locator;

/* stravaV10's original named this VUE_GPS_NB_LINES -- a copy-paste
 * artifact from VueGPS.cpp visible in its own header comment ("Created
 * on: 27 f�vr. 2018 / VueGPS.cpp" atop a file named VueDebug.cpp).
 * Renamed for clarity; same value (7), same meaning (row count for the
 * cadran()/cadranH() layout math below). */
#define VUE_DEBUG_NB_LINES 7

VueDebug::VueDebug() : Adafruit_GFX(0, 0)
{
}

void VueDebug::displayDebug()
{
	String info = "";

	vue.setCursor(20, 20);
	vue.setTextSize(2);

	locator.displayGPS2();

	vue.println("  ----- SNS -----");

	float current_ma = 0.f, voltage_v = 0.f;
	bool has_stc = debug_screen_get_stc_current_ma(&current_ma);

	(void)debug_screen_get_stc_voltage_v(&voltage_v);

	info = String(" STC: ") + String((int)current_ma);
	info += "mA @ ";
	info += String((int)(voltage_v * 1000.f));
	info += "mV";
	vue.println(info);

	vue.println("  ----- MEM -----");

	String segs = " ";
	segs += debug_screen_get_segment_count();
	segs += " segments loaded";
	vue.println(segs);

	this->cadranH(6, VUE_DEBUG_NB_LINES, "Time", _timemkstr(att.date, ':'), NULL);

	this->cadran(7, VUE_DEBUG_NB_LINES, 1, "STC", _imkstr((int)current_ma), "mA");
	this->cadran(7, VUE_DEBUG_NB_LINES, 2, "SOC", has_stc ? _imkstr((int)percentageBatt(voltage_v, current_ma)) : "--", "%");
}
