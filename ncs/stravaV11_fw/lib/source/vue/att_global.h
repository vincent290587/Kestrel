/*
 * GFX port Phase D: a minimal stand-in for stravaV10's global `SAtt att;`
 * (normally declared in Model.h, defined via source/model/Attitude.h).
 * VueDebug.cpp's only real dependency on it is `att.date` (a plain SDate
 * field, already available via Locator.h). Deliberately NOT including the
 * real Attitude.h: it unconditionally pulls in "hardfault.h" (SDK16
 * fault-handling glue, "trash" category per the original architecture
 * survey, not ported) even for callers that only want the plain struct
 * layout, and its C++ section declares the full Attitude class (needs the
 * FXOS sensor driver + AltiBaro, neither ported -- Attitude.cpp is a
 * genuinely separate, not-yet-done piece of work, same as Boucle*).
 *
 * SAtt is otherwise just a passive data snapshot -- normally populated
 * over time by Attitude::computeFusion() (not ported), so this global
 * stays at its zero-initialized default until that class exists. `att.date`
 * reads as a zeroed SDate (secj=0, date=0, timestamp=0) until then, which
 * is an honest reflection of "no real attitude/elevation fusion has run
 * yet", not a fake value.
 */

#ifndef SOURCE_VUE_ATT_GLOBAL_H_
#define SOURCE_VUE_ATT_GLOBAL_H_

#include <stdint.h>
#include "Locator.h"

typedef struct {
	SLoc loc;
	SDate date;
	float climb;
	float vit_asc;
	int8_t slope;
	float dist;
	int16_t pwr;
	uint16_t nbpts;
	uint8_t nbact;
	uint8_t pr;
	uint16_t nbsec_act;
	uint16_t next;
} SAtt;

extern SAtt att;

#endif /* SOURCE_VUE_ATT_GLOBAL_H_ */
