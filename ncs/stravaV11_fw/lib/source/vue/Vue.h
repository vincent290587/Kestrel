/*
 * Vue.h
 *
 *  Created on: 12 déc. 2017
 *      Author: Vincent
 *
 * GFX port Phase D (Vue assembly): the diamond this class assembles is
 * unchanged from stravaV10's original (VueCRS/VueFEC/VuePRC/VueDebug all
 * `virtual public Adafruit_GFX`, merged here) with one deliberate addition
 * -- `ZephyrGFX` joins the same virtual-Adafruit_GFX diamond (see that
 * class's own comment on why its base had to become `virtual` for this).
 * That addition means Vue does NOT need stravaV10's own drawPixel()/
 * drawPixelGroup()/fillRoundRect()/drawFastVLine()/fillRect() overrides
 * at all -- ZephyrGFX already provides a validated, buffer-native
 * drawPixel()/drawFastVLine()/drawFastHLine()/fillScreen() (Phase 7/B),
 * and Adafruit_GFX's own default fillRoundRect() (unused by anything
 * ported so far -- confirmed by grepping stravaV10/source/vue/*.cpp) covers
 * that one too. This is a real, deliberate simplification, not a gap:
 * porting stravaV10's own hand-rolled LS027_drawPixel()-based rotation
 * math a second time, when ZephyrGFX already implements the equivalent
 * transform against this port's actual display backend, would just be
 * duplicated logic with two chances to drift apart.
 *
 * <button.h>/<ls027.h> dropped from the include list: eButtonsEvent comes
 * from <vue/button.h> (this port's own copy, already used by every other
 * Vue* header) not a bare <button.h>; ls027.h was stravaV10's own SDK16
 * LCD driver header, with no equivalent needed here (see Vue.cpp's own
 * note on init()/clearDisplay()).
 */

#ifndef SOURCE_VUE_VUE_H_
#define SOURCE_VUE_VUE_H_

#include <vue/VueCommon.h>
#include <vue/Notif.h>
#include <vue/VueCRS.h>
#include <vue/VueFEC.h>
#include <vue/VuePRC.h>
#include <vue/VueDebug.h>
#include <vue/VueLap.h>
#include <vue/Menuable.h>
#include <vue/button.h>
#include <vue/ZephyrGFX.h>

typedef enum {
	eVueGlobalScreenCRS,
	eVueGlobalScreenFEC,
	eVueGlobalScreenPRC,
	eVueGlobalScreenDEBUG,
	/* Manual-lap feature (2026-09-12): appended at the end so the
	 * existing four values keep their numeric meaning -- vue_demo.h's
	 * VUE_MODE_DEBUG/VUE_MODE_LAP plain-int macros (cmd_console.c's
	 * "VUE LAP"/"VUE DEBUG" commands) must stay in sync with this
	 * ordering. */
	eVueGlobalScreenLAP,
} eVueGlobalScreenModes;

class Vue: public VueCRS, public VueFEC, public VuePRC, public VueDebug, public VueLap, public NotifiableDevice, public Menuable, public ZephyrGFX {
public:
	Vue();

	void init(void);

	void tasks(eButtonsEvent event);

	void setCurrentMode(eVueGlobalScreenModes mode_);

	void refresh(void) override;

	/* GFX port Phase D: stravaV10's clearDisplay()/invertDisplay()/
	 * writeWhole() each called a specific SDK16 LS027 driver function
	 * (LS027_Clear()/LS027_InvertColors()/LS027_UpdateFull()) with no
	 * this-port equivalent -- see Vue.cpp's own note on each. */
	void clearDisplay(void);
	void invertDisplay(void);
	void writeWhole(void) {
		/* no-op: this port's display path (ZephyrGFX -> display_write())
		 * always transfers the whole buffer already, so there's no
		 * separate "partial vs. full" mode for this to select between. */
	}

	uint32_t getLastRefreshed() const {
		return m_last_refreshed;
	}

	void cadran (uint8_t p_lig, uint8_t nb_lig, uint8_t p_col, const char *champ, String  affi, const char *p_unite) override;
	void cadranH(uint8_t p_lig, uint8_t nb_lig, const char *champ, String  affi, const char *p_unite) override;

	void cadranRR(uint8_t p_lig, uint8_t nb_lig, uint8_t p_col, const char *champ, RRZone &zone) override;

	void Histo(uint8_t p_lig, uint8_t nb_lig, uint8_t p_col, sVueHistoConfiguration& h_config_) override;
	void HistoH (uint8_t p_lig, uint8_t nb_lig, sVueHistoConfiguration& h_config_) override;

private:
	eVueGlobalScreenModes m_global_mode;
	uint32_t m_last_refreshed;

};

#endif /* SOURCE_VUE_VUE_H_ */
