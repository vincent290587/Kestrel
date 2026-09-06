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

#endif /* BSC_DEMO_H_ */
