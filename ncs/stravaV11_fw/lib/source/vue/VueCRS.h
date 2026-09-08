/*
 * VueCRS.h
 *
 *  Created on: 12 déc. 2017
 *      Author: Vincent
 *
 * GFX port Phase D: unmodified from stravaV10's original except for one
 * added include -- <vue/VueCommon.h> for sVueHistoConfiguration (Histo()/
 * HistoH()'s param type). The original got it transitively through
 * Model.h; VueFEC.h already needed the same explicit include once
 * Model.h left the picture, so this just follows that same precedent.
 * Every other type this header names (RRZone, SegmentManager/Segment,
 * VueGPS, button.h's eButtonsEvent) is already ported and reachable via
 * the same include paths every other Vue* header in this port already
 * uses.
 */

#ifndef SOURCE_VUE_VUECRS_H_
#define SOURCE_VUE_VUECRS_H_

#include "parameters.h"
#include "RRZone.h"
#include <display/SegmentManager.h>
#include <Adafruit_GFX.h>
#include <vue/VueGPS.h>
#include <vue/VueCommon.h>
#include <vue/button.h>


typedef enum {
	eVueCRSScreenInit,
	eVueCRSScreenDataFull,
	eVueCRSScreenDataSS,
	eVueCRSScreenDataDS,
} eVueCRSScreenModes;

typedef enum {
	eVueCRSScreenPage1,
	eVueCRSScreenPage2,
	eVueCRSScreenPage3,
} eVueCRSScreenPage;

class VueCRS: virtual public Adafruit_GFX, virtual public VueGPS {
public:
	VueCRS();

	eVueCRSScreenModes tasksCRS();

	virtual void cadranH(uint8_t p_lig, uint8_t nb_lig, const char *champ, String  affi, const char *p_unite)=0;
	virtual void cadran(uint8_t p_lig, uint8_t nb_lig, uint8_t p_col, const char *champ, String  affi, const char *p_unite)=0;
	virtual void cadranRR(uint8_t p_lig, uint8_t nb_lig, uint8_t p_col, const char *champ, RRZone &zone)=0;

	virtual void Histo(uint8_t p_lig, uint8_t nb_lig, uint8_t p_col, sVueHistoConfiguration& h_config_)=0;
	virtual void HistoH (uint8_t p_lig, uint8_t nb_lig, sVueHistoConfiguration& h_config_)=0;

	bool propagateEventsCRS(eButtonsEvent event);

protected:
	void partner(uint8_t ligne, Segment *p_seg);

	eVueCRSScreenModes m_crs_screen_mode;

	/* GFX port Phase D: stravaV10's original has these `private`. Widened
	 * to `protected` -- same deliberate, documented, non-fidelity-loss
	 * deviation as VuePRC.h's own afficheSegment()/afficheParcours() --
	 * so the native_sim test harness can exercise them directly with real
	 * constructed Segment data, independently of tasksCRS() (which needs
	 * segMngr to actually hold segments to reach eVueCRSScreenDataSS/DS
	 * at all). No change to external behavior: still unreachable from
	 * outside the VueCRS hierarchy. */
	void afficheSegment(uint8_t ligne, Segment *p_seg);
	void afficheScreen1(void);
	void afficheScreen2(void);
	void afficheSensors(void);

	eVueCRSScreenPage m_screen_page;
};

#endif /* SOURCE_VUE_VUECRS_H_ */
