#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "Locator.h"
#include "gps_sim_demo.h"
#include "gps_uart_demo.h"

LOG_MODULE_REGISTER(gps_sim_demo, LOG_LEVEL_INF);

#define GPS_SIM_PERIOD_MS 1000

/* Rotterdam-ish start point, matching the flavor of the real recorded
 * ride this port's own tools/gpx_to_c.py once replayed (see CLAUDE.md's
 * maps-feasibility-study section) -- not the same file (that one's gone,
 * see gps_sim_demo.h's own history note), just a plausible, real-world
 * coordinate rather than (0,0). */
#define GPS_SIM_START_LAT 51.9225f
#define GPS_SIM_START_LON 4.4750f
#define GPS_SIM_START_ALT_M 5
#define GPS_SIM_SPEED_KMH 25.0f
#define GPS_SIM_HEADING_DEG 90.0f /* due east */

/* Fixed start date/time -- a short simulated ride never crosses midnight,
 * so no day-rollover handling is needed. */
#define GPS_SIM_START_HOUR 10
#define GPS_SIM_START_MIN 0
#define GPS_SIM_START_SEC 0
#define GPS_SIM_START_DAY 12
#define GPS_SIM_START_MONTH 9
#define GPS_SIM_START_YEAR 26 /* 2-digit, NMEA ddmmyy */

static bool s_running;
static float s_lat = GPS_SIM_START_LAT;
static float s_lon = GPS_SIM_START_LON;
static int s_hour = GPS_SIM_START_HOUR;
static int s_min = GPS_SIM_START_MIN;
static int s_sec = GPS_SIM_START_SEC;
static uint32_t s_tick_count;

static void gps_sim_tick_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(gps_sim_tick_work, gps_sim_tick_handler);

/* NMEA checksum: XOR of every byte between '$' and '*' (both excluded). */
static uint8_t nmea_checksum(const char *body)
{
	uint8_t cs = 0;

	for (const char *p = body; *p != '\0'; p++) {
		cs ^= (uint8_t)*p;
	}
	return cs;
}

static void feed_sentence(const char *body)
{
	char line[96];
	int len = snprintf(line, sizeof(line), "$%s*%02X\r\n", body, nmea_checksum(body));

	for (int i = 0; i < len && i < (int)sizeof(line); i++) {
		locator_encode_char(line[i]);
	}
}

/* Decimal degrees -> NMEA "ddmm.mmmm"/"dddmm.mmmm" (degree_digits controls
 * 2 vs 3 leading digits, lat vs lon) -- fixed-point only, no %f, same
 * caution gps_demo.cpp's own log formatting already established (picolibc
 * printf-under-printk()'s float support isn't confirmed in this build). */
static void format_nmea_coord(float abs_deg, int degree_digits, char *out, size_t out_len)
{
	int deg = (int)abs_deg;
	float min_full = (abs_deg - deg) * 60.0f;
	int min_int = (int)min_full;
	int min_frac = (int)((min_full - min_int) * 10000.0f + 0.5f);

	if (min_frac >= 10000) {
		min_frac -= 10000;
		min_int += 1;
	}

	if (degree_digits == 3) {
		snprintf(out, out_len, "%03d%02d.%04d", deg, min_int, min_frac);
	} else {
		snprintf(out, out_len, "%02d%02d.%04d", deg, min_int, min_frac);
	}
}

static void gps_sim_tick_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!s_running) {
		return;
	}

	/* Advance position: speed_kmh over 1 real second, at a fixed
	 * heading. 1 degree latitude is ~111320m everywhere; 1 degree
	 * longitude shrinks by cos(latitude). */
	float dist_m = GPS_SIM_SPEED_KMH * 1000.0f / 3600.0f * (GPS_SIM_PERIOD_MS / 1000.0f);
	float heading_rad = GPS_SIM_HEADING_DEG * 3.14159265f / 180.0f;

	s_lat += (dist_m * cosf(heading_rad)) / 111320.0f;
	s_lon += (dist_m * sinf(heading_rad)) / (111320.0f * cosf(s_lat * 3.14159265f / 180.0f));

	s_sec++;
	if (s_sec >= 60) {
		s_sec = 0;
		s_min++;
		if (s_min >= 60) {
			s_min = 0;
			s_hour++;
		}
	}

	char lat_str[16], lon_str[16];

	format_nmea_coord(fabsf(s_lat), 2, lat_str, sizeof(lat_str));
	format_nmea_coord(fabsf(s_lon), 3, lon_str, sizeof(lon_str));

	char lat_hemi = (s_lat >= 0) ? 'N' : 'S';
	char lon_hemi = (s_lon >= 0) ? 'E' : 'W';

	/* Speed in knots, fixed-point tenths (1 knot = 1.852 km/h). */
	int speed_knots_x10 = (int)(GPS_SIM_SPEED_KMH / 1.852f * 10.0f + 0.5f);

	char body[80];

	snprintf(body, sizeof(body),
		 "GPRMC,%02d%02d%02d.00,A,%s,%c,%s,%c,%d.%d,%03d.0,%02d%02d%02d,,,A", s_hour,
		 s_min, s_sec, lat_str, lat_hemi, lon_str, lon_hemi, speed_knots_x10 / 10,
		 speed_knots_x10 % 10, (int)GPS_SIM_HEADING_DEG, GPS_SIM_START_DAY,
		 GPS_SIM_START_MONTH, GPS_SIM_START_YEAR);
	feed_sentence(body);

	snprintf(body, sizeof(body), "GPGGA,%02d%02d%02d.00,%s,%c,%s,%c,1,08,0.9,%d.0,M,0.0,M,,",
		 s_hour, s_min, s_sec, lat_str, lat_hemi, lon_str, lon_hemi,
		 GPS_SIM_START_ALT_M);
	feed_sentence(body);

	s_tick_count++;
	if ((s_tick_count % 10) == 0) {
		LOG_INF("gps_sim: tick %u, lat=%d.%06d lon=%d.%06d", s_tick_count, (int)s_lat,
			(int)(fabsf(s_lat - (int)s_lat) * 1000000), (int)s_lon,
			(int)(fabsf(s_lon - (int)s_lon) * 1000000));
	}

	k_work_schedule(&gps_sim_tick_work, K_MSEC(GPS_SIM_PERIOD_MS));
}

void gps_sim_demo_start(void)
{
	if (s_running) {
		LOG_WRN("gps_sim: already running");
		return;
	}

	s_running = true;
	s_lat = GPS_SIM_START_LAT;
	s_lon = GPS_SIM_START_LON;
	s_hour = GPS_SIM_START_HOUR;
	s_min = GPS_SIM_START_MIN;
	s_sec = GPS_SIM_START_SEC;
	s_tick_count = 0;

	/* Real and simulated GPS both feed the same shared Locator via
	 * locator_encode_char() -- see gps_uart_demo.c's own comment for why
	 * this is mutual exclusion, not a race between two independent
	 * sources the way ANT+/BLE power/HRM/cadence work. */
	gps_uart_demo_pause();

	LOG_INF("gps_sim: started (start lat=%d.%04d lon=%d.%04d, %d km/h heading %d deg)",
		(int)GPS_SIM_START_LAT, (int)(fabsf(GPS_SIM_START_LAT - (int)GPS_SIM_START_LAT) * 10000),
		(int)GPS_SIM_START_LON, (int)(fabsf(GPS_SIM_START_LON - (int)GPS_SIM_START_LON) * 10000),
		(int)GPS_SIM_SPEED_KMH, (int)GPS_SIM_HEADING_DEG);

	k_work_schedule(&gps_sim_tick_work, K_NO_WAIT);
}

void gps_sim_demo_stop(void)
{
	s_running = false;
	k_work_cancel_delayable(&gps_sim_tick_work);
	gps_uart_demo_resume();
	LOG_INF("gps_sim: stopped after %u ticks", s_tick_count);
}
