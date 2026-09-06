#ifndef GPS_DEMO_H_
#define GPS_DEMO_H_

#ifdef __cplusplus
extern "C" {
#endif

/* One-time Locator setup -- call before feeding any bytes. */
void gps_demo_init(void);

/* Logs whatever Locator has decoded so far (GPS fix, or "no update"). */
void gps_demo_report(void);

#ifdef __cplusplus
}
#endif

#endif /* GPS_DEMO_H_ */
