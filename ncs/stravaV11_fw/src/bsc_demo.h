#ifndef BSC_DEMO_H_
#define BSC_DEMO_H_

#include <stdbool.h>
#include <stdint.h>

/* Call after ant_demo_start() (ANT+ network key is programmed once for
 * network 0, shared with the wildcard demo channel and hrm_demo). */
int bsc_demo_start(void);

/* Last computed speed (km/h) and cadence (rpm), for display/other
 * consumers -- see calculate_speed()/calculate_cadence() in bsc_demo.c.
 * Stale (last-known) values are returned even while unpaired; check
 * bsc_demo_is_paired() to tell "live" from "last known". */
uint32_t bsc_demo_get_speed(void);
uint32_t bsc_demo_get_cadence(void);
bool bsc_demo_is_paired(void);

/* Milliseconds since the last real ANT+ combined-page update (speed and
 * cadence always update together, from the same page -- see
 * ant_bsc_evt_handler()), or UINT32_MAX if none ever arrived. Same
 * age-vs-is_paired() distinction as hrm_demo_get_age_ms() -- see there. */
uint32_t bsc_demo_get_age_ms(void);

#endif /* BSC_DEMO_H_ */
