/*
 * Phase 11: wires the real GPS UART bytes (uart_demo() in main.c) into the
 * Locator/TinyGPS++ logic ported in Phase 6 (stravaV11_app, native_sim only
 * until now) -- same files, copied unmodified, not reimplemented. Locator
 * itself is C++-only (Locator.h guards the class behind #if
 * defined(__cplusplus)); this file is the thin C-linkage wrapper main.c (a
 * plain C file) needs to own a Locator instance and read its output.
 * locator_encode_char() itself is already extern "C" in Locator.h, so
 * main.c calls that one directly -- this wrapper only covers what a C file
 * can't do itself: constructing Locator and calling its C++ methods.
 *
 * Deliberately fixed-point in the log output (lat/lon *1e6, others *100),
 * not %f -- picolibc's printf under printk() here isn't confirmed to
 * support floats, and this avoids finding out the hard way with silent
 * garbage output.
 */

#include "gps_demo.h"

#include <math.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "Locator.h"

static Locator s_locator;

void gps_demo_init(void)
{
	s_locator.init();
}

void gps_demo_inject_location(float lat, float lon, float alt, float speed, float course,
			       uint32_t utc_time, uint32_t date)
{
	sLocationData data = {};

	data.lat = lat;
	data.lon = lon;
	data.alt = alt;
	data.speed = speed;
	data.course = course;
	data.utc_time = utc_time;
	data.utc_timestamp = (uint32_t)k_uptime_get();
	data.date = date;

	/* Sensor<T>::operator= does the memcpy + setIsUpdated() -- same as a
	 * real fix flowing through Locator::tasks() would. */
	s_locator.gps_loc = data;
}

void gps_demo_report(void)
{
	SLoc loc = {};
	SDate date = {};
	eLocationSource src = s_locator.getPosition(loc, date);

	if (src != eLocationSourceGPS) {
		printk("locator: no GPS update yet (source=%d)\n", (int)src);
		return;
	}

	printk("locator: GPS fix lat=%d.%06d lon=%d.%06d alt=%d.%02dm speed=%d.%02dkm/h course=%d.%02d\n",
	       (int)loc.lat, (int)(fabsf(loc.lat - (int)loc.lat) * 1000000),
	       (int)loc.lon, (int)(fabsf(loc.lon - (int)loc.lon) * 1000000),
	       (int)loc.alt, (int)(fabsf(loc.alt - (int)loc.alt) * 100),
	       (int)loc.speed, (int)(fabsf(loc.speed - (int)loc.speed) * 100),
	       (int)loc.course, (int)(fabsf(loc.course - (int)loc.course) * 100));
}
