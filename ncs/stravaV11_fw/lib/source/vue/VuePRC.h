/*
 * VuePRC.h
 *
 *  Created on: 12 d�c. 2017
 *      Author: Vincent
 *
 * GFX port Phase D: dropped the "#include <vue/VueCRS.h>" -- nothing in
 * this class actually references a VueCRS-specific type (VuePRC inherits
 * Adafruit_GFX/VueGPS/Zoom, not VueCRS; afficheSegment()'s Segment/
 * sVueCRSPSeg types come from SegmentManager.h/Segment.h, already
 * included directly), and dropping it means this file doesn't drag in
 * VueCRS's own dependencies before VueCRS itself is ported. Everything
 * else here is unmodified.
 */

#ifndef SOURCE_VUE_VUEPRC_H_
#define SOURCE_VUE_VUEPRC_H_

#include <Adafruit_GFX.h>
#include <display/Zoom.h>
#include <display/SegmentManager.h>
#include <routes/Parcours.h>
#include <vue/VueGPS.h>
#include <vue/button.h>

typedef enum {
	eVuePRCScreenInit,
	eVuePRCScreenGps,
	eVuePRCScreenDataFull,
} eVuePRCScreenModes;

class VuePRC: virtual public Adafruit_GFX, virtual public VueGPS, protected Zoom {
public:
	VuePRC();

	eVuePRCScreenModes tasksPRC();

	bool propagateEventsPRC(eButtonsEvent event);

	void displayLoading();

	virtual void cadranH(uint8_t p_lig, uint8_t nb_lig, const char *champ, String  affi, const char *p_unite)=0;
	virtual void cadran(uint8_t p_lig, uint8_t nb_lig, uint8_t p_col, const char *champ, String  affi, const char *p_unite)=0;

protected:
	eVuePRCScreenModes m_prc_screen_mode;
	float m_distance_prc;

	/* GFX port Phase D: stravaV10's original has these `private`.
	 * Widened to `protected` -- a deliberate, small deviation, not a
	 * fidelity slip -- so the native_sim test harness (a class deriving
	 * from VuePRC, same pattern already used for VueFEC's protected
	 * tasksFEC()) can exercise them directly with real constructed
	 * Parcours/Segment data, independently of tasksPRC() (which can't
	 * reach eVuePRCScreenDataFull on its own right now -- see
	 * VuePRC.cpp's own note on why). No change to external behavior:
	 * still unreachable from outside the VuePRC hierarchy. */
	void afficheSegment(uint8_t ligne, Segment *p_seg);
	void afficheParcours(uint8_t ligne, ListePoints2D *p_liste);

};

#endif /* SOURCE_VUE_VUEPRC_H_ */
