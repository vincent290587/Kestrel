#ifndef HRM_DEMO_H_
#define HRM_DEMO_H_

#include <stdbool.h>
#include <stdint.h>

/* Call after ant_demo_start() (ANT+ network key is programmed once for
 * network 0, shared with the wildcard demo channel and bsc_demo). */
int hrm_demo_start(void);

/* Last decoded heart rate (bpm) and R-R interval (ms), for display/other
 * consumers -- see ant_hrm_evt_handler() in hrm_demo.c for how these are
 * computed. Stale (last-known) values are returned even while unpaired;
 * check hrm_demo_is_paired() to tell "live" from "last known". */
uint8_t hrm_demo_get_bpm(void);
uint16_t hrm_demo_get_rr_ms(void);
bool hrm_demo_is_paired(void);

#endif /* HRM_DEMO_H_ */
