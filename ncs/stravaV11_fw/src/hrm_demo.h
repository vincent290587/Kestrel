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

/* Milliseconds since the last real ANT+ page was decoded, or UINT32_MAX if
 * none ever arrived. Genuinely age-based, unlike hrm_demo_is_paired() --
 * that flag is only cleared on EVENT_CHANNEL_CLOSED, not on
 * EVENT_RX_SEARCH_TIMEOUT (a strap gone out of range without the channel
 * formally closing stays "paired" with an increasingly stale bpm). Added
 * for the GFX port's Phase C model-glue shim (see todo.md) to detect a
 * disconnected sensor that hasn't triggered a channel close. */
uint32_t hrm_demo_get_age_ms(void);

#endif /* HRM_DEMO_H_ */
