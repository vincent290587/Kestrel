/*
 * Screenutils.h
 *
 *  Created on: 13 d�c. 2017
 *      Author: Vincent
 */

#ifndef SOURCE_VUE_SCREENUTILS_H_
#define SOURCE_VUE_SCREENUTILS_H_

/* stravaV10's original included Attitude.h here, but nothing in this file
 * (or Screenutils.cpp) actually references Attitude/SAtt -- a dead include
 * left over from Screenutils.h being edited alongside other vue/ headers
 * that do need it. Dropped: Attitude.cpp isn't ported (Phase 6 note), and
 * this file has no real dependency on it. Included directly instead of
 * transitively: <stdint.h>/<math.h> (TWO_PI/int16_t), WString.h (String),
 * Locator.h (SDate, used by _timemkstr()). */
#include <stdint.h>
#include <math.h>
#include "WString.h"
#include "Locator.h"

#define TWO_PI (2.f*M_PI)

#define CLIP(X,Y,Z) (MIN(MAX(X,Y),Z))

void rotate_point(float angle, int16_t cx, int16_t cy,
		int16_t x1, int16_t y1, int16_t &x2, int16_t &y2);

float course_to (float lat1, float long1, float lat2, float long2);

String _imkstr(int value);

String _fmkstr(float value, unsigned int nb_digits);

String _secjmkstr(uint32_t value, char sep);

String _timemkstr(SDate& date_, char sep);

#endif /* SOURCE_VUE_SCREENUTILS_H_ */
