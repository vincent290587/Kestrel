/*
 * VueLap.h
 *
 * Manual-lap feature (2026-09-12): a new Vue screen showing the current
 * (still-open) lap and the last few closed ones. Styled after VueDebug
 * -- the simplest existing screen with real, live-wired data -- rather
 * than VueCRS/VueFEC/VuePRC's much heavier paging/GPS/segment diamond,
 * since this screen has none of that: just cadran()/cadranH() cells
 * fed by lap_screen_data.h. stravaV11_fw-only, see that header's own
 * comment for why.
 */

#ifndef SOURCE_VUE_VUELAP_H_
#define SOURCE_VUE_VUELAP_H_

#include <Adafruit_GFX.h>

class VueLap: virtual public Adafruit_GFX {
public:
	VueLap();

	virtual void cadranH(uint8_t p_lig, uint8_t nb_lig, const char *champ, String affi, const char *p_unite) = 0;
	virtual void cadran(uint8_t p_lig, uint8_t nb_lig, uint8_t p_col, const char *champ, String affi, const char *p_unite) = 0;

	void displayLap();
};

#endif /* SOURCE_VUE_VUELAP_H_ */
