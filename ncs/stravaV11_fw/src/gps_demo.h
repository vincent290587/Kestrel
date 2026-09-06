#ifndef GPS_DEMO_H_
#define GPS_DEMO_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One-time Locator setup -- call before feeding any bytes. */
void gps_demo_init(void);

/* Logs whatever Locator has decoded so far (GPS fix, or "no update"). */
void gps_demo_report(void);

/* Injects a fix directly into Locator's gps_loc state, bypassing NMEA/
 * TinyGPS++ entirely -- for gps_sim_demo.c's route replay (lab GPS testing
 * without real sky visibility). speed is km/h, course is degrees, utc_time
 * is seconds since midnight UTC, date is Locator's own (year%100) +
 * day*10000 + month*100 encoding (see Locator.cpp's tasks()). Marks the fix
 * updated exactly as a real one would (Sensor<T>::operator= calls
 * setIsUpdated()), so it flows through Locator::getPosition() normally. */
void gps_demo_inject_location(float lat, float lon, float alt, float speed, float course,
			       uint32_t utc_time, uint32_t date);

/* Latest GPS-fix altitude in metres, for the LIVE DATA screen
 * (sensor_screen_demo.c). Writes *alt_m and returns true only when a fix
 * is currently available (Locator::getPosition() reports
 * eLocationSourceGPS); returns false and leaves *alt_m untouched
 * otherwise -- same "don't show stale data as live" convention as
 * hrm_demo_is_paired()/bsc_demo_is_paired(). */
bool gps_demo_get_altitude(float *alt_m);

#ifdef __cplusplus
}
#endif

#endif /* GPS_DEMO_H_ */
