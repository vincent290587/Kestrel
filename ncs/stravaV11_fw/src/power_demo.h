#ifndef POWER_DEMO_H_
#define POWER_DEMO_H_

void power_demo_start(void);

/* Replaces power_scheduler__ping() -- call whenever the app is "active"
 * (stravaV10 calls the equivalent from the CRS/FEC ride loops). */
void power_demo_ping(void);

#endif /* POWER_DEMO_H_ */
