#ifndef FEC_DEMO_H_
#define FEC_DEMO_H_

#include <stdbool.h>
#include <stdint.h>

#include "mk64f_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

int fec_demo_start(void);

/* Sets the trainer control target -- mirrors stravaV10's fec_set_control()
 * signature/semantics exactly (see rf/fec.c's roller_manager()), so a
 * future Model.cpp/UI wiring can call this the same way. Takes effect on
 * the next periodic TX tick (roughly every 2s), matching stravaV10's own
 * "one pending control message at a time" behavior. */
void fec_demo_set_control(const sFecControl *control);

uint16_t fec_demo_get_power_w(void);
uint16_t fec_demo_get_elapsed_time_s(void);
bool fec_demo_is_paired(void);

#ifdef __cplusplus
}
#endif

#endif /* FEC_DEMO_H_ */
