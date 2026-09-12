/*
 * Vue.cpp
 *
 *  Created on: 12 déc. 2017
 *      Author: Vincent
 *
 * GFX port Phase D (Vue assembly): dropped sysview_task_void_enter()/
 * exit() SystemView profiling calls and w_task_yield() (task_manager,
 * both SDK16 glue, not ported, same as every other file in this port).
 *
 * drawPixel()/drawPixelGroup()/fillRoundRect()/drawFastVLine()/fillRect()
 * are NOT ported here at all -- see Vue.h's own note: ZephyrGFX (one of
 * this class's bases now) already provides the buffer-native equivalents,
 * and duplicating stravaV10's own hand-rolled LS027-specific pixel/
 * rotation math on top would just be the same logic twice.
 *
 * init(): dropped the LS027_Init() call -- SDK16's own LCD driver init,
 * with no equivalent needed here. This port's actual display device init
 * (Zephyr's `sharp,ls0xx` driver, via devicetree) is owned by
 * stravaV11_fw's own boot code (see gfx_demo.c), not by Vue itself -- Vue
 * only owns the in-memory pixel buffer (via ZephyrGFX) and never touches
 * a Zephyr device handle directly, keeping it buildable on native_sim
 * with no hardware includes, same as every other class in this file's
 * diamond.
 *
 * clearDisplay(): stravaV10's LS027_Clear() became this->fillScreen(1)
 * (white background, this port's established convention -- see
 * ZephyrGFX.h's own comment on 1=white/0=black). invertDisplay():
 * stravaV10's LS027_InvertColors() has no direct equivalent in this
 * port's display path (the ls0xx driver exposes no color-invert
 * primitive, and ZephyrGFX doesn't expose a writable buffer accessor to
 * implement one externally) -- and nothing in the ported codebase (traced
 * by grepping stravaV10/source/ directly) ever actually calls it besides
 * Adafruit_GFX's own unrelated invertDisplay(bool), a different method
 * Vue's own parameterless version merely shadows, not overrides. Kept as
 * a real, working implementation anyway (XORing the whole buffer, since
 * ZephyrGFX's buffer format is a plain bit-per-pixel array) rather than a
 * no-op stub, since it's simple, genuinely correct, and cheap to keep
 * available for whenever a real caller wants it.
 *
 * cadran()/cadranH()'s `printRev()` and HistoH()'s `drawFastPHLine()` are
 * both already-ported Adafruit_GFX/Print methods (Phase 7) -- no
 * substitution needed, straight port.
 *
 * refresh()'s notification banner had one real, fixed bug, not a
 * preserved quirk: stravaV10's own `if (text > 2)` compares a String
 * against the *lexicographic* result of implicitly converting the int 2
 * to a String("2") (String::operator>(const String&) is the only
 * matching overload) -- not the length check the surrounding code clearly
 * intends (skip getTextBounds() for a near-empty string). Since almost
 * any real notification text starts with a letter (ASCII code higher
 * than the digit '2'), the original's check was true almost always
 * regardless of actual length -- a latent, likely-harmless bug (never
 * skips the intended-cheap path when it should), but a bug, not a
 * deliberate design choice, and correctable with confidence (unlike the
 * unresolved ANT+ FE-C page 51 constant elsewhere in this port, this one
 * has no cross-referenceable external spec to weigh against -- the
 * intent is unambiguous from the surrounding code itself). Fixed to
 * `text.length() > 2`, the evident original intent.
 */

#include <vue/Vue.h>
#include "millis.h"
#include "nordic_common.h"
#include "segger_wrapper.h"
#include "assert_wrapper.h"
#include "WString.h"
#include "Org_01.h"
#include "parameters.h"
#include "utils.h"

Vue::Vue() : Adafruit_GFX(ZEPHYR_GFX_WIDTH, ZEPHYR_GFX_HEIGHT) {

	m_global_mode = VUE_DEFAULT_MODE;
	m_last_refreshed = 0;

}

void Vue::init(void) {
	this->setRotation(3);
	this->initMenu();
	this->setTextWrap(false);
	this->setFont(&Org_01);
}

void Vue::tasks(eButtonsEvent event) {

	switch (m_global_mode) {
	case eVueGlobalScreenCRS:
	{
		// propagate to the inner vue
		if (!m_is_menu_selected) {
			this->propagateEventsCRS(event);
		}
		this->propagateEvent(event);
		break;
	}
	case eVueGlobalScreenFEC:
	{
		// propagate to the inner vue
		this->propagateEvent(event);
		break;
	}
	case eVueGlobalScreenPRC:
	{
		if (!m_is_menu_selected) {
			this->propagateEventsPRC(event);
		}
		this->propagateEvent(event);
		break;
	}
	case eVueGlobalScreenDEBUG:
	{
		// propagate to the inner menu
		this->propagateEvent(event);
		break;
	}
	case eVueGlobalScreenLAP:
	{
		/* Manual-lap feature (2026-09-12): no button interaction of its
		 * own yet (this screen is only reachable via the "VUE LAP"
		 * console command, see vue_demo.cpp) -- still propagates so the
		 * shared notification-banner state machine (NotifiableDevice)
		 * keeps working the same as every other screen. */
		this->propagateEvent(event);
		break;
	}
	default:
		break;
	}

}

void Vue::setCurrentMode(eVueGlobalScreenModes mode_) {

	m_global_mode = mode_;

}

void Vue::clearDisplay(void) {
	this->fillScreen(1);
}

void Vue::invertDisplay(void) {
	uint8_t *buf = const_cast<uint8_t *>(this->getBuffer());
	for (size_t i = 0; i < this->getBufferSize(); i++) {
		buf[i] = ~buf[i];
	}
}

void Vue::refresh(void) {

	m_last_refreshed = millis();

	this->clearDisplay();

	if (m_is_menu_selected) {
		this->tasksMenu();
	} else {
		switch (m_global_mode) {
		case eVueGlobalScreenCRS:
			this->tasksCRS();
			break;
		case eVueGlobalScreenFEC:
			this->tasksFEC();
			break;
		case eVueGlobalScreenPRC:
			this->tasksPRC();
			break;
		case eVueGlobalScreenDEBUG:
			this->displayDebug();
			break;
		case eVueGlobalScreenLAP:
			this->displayLap();
			break;

		default:
			break;
		}
	}

	// Notifications tasks
	if (m_notifs.size()) {

		this->setTextWrap(true);

		Notif& notif = m_notifs.front();

		this->setCursor(5, 5);
		this->setTextSize(2);

		String text;

		if (eNotificationTypeComplete == notif.m_type) {
			text = notif.m_title;
			text += ": ";
		}

		text += notif.m_msg;

		int16_t x1=0; int16_t y1=0; uint16_t w=0; uint16_t h=0;
		if (text.length() > 2)
			this->getTextBounds((char*)text.c_str(), this->getCursorX(), this->getCursorY(), &x1, &y1, &w, &h);

		h += this->getCursorY() + 15;
		this->drawFastHLine(0, h, _width, textcolor);
		this->fillRect(0, 0, _width, h, textbgcolor);
		this->print(text);

		if (notif.m_persist-- == 0) {
			m_notifs.pop_front();
		}

		this->setTextWrap(false);
	}

}

void Vue::cadranH(uint8_t p_lig, uint8_t nb_lig, const char *champ, String  affi, const char *p_unite) {

	int decal = 0;
	int x = _width / 4;
	int y = _height / nb_lig * (p_lig - 1);

	setCursor(5, y + 8);
	setTextSize(1);

	if (champ) print(champ);

	if (affi.length() > 9) {
		affi = "-----";
	} else {
		decal = (4 - affi.length()) * 6;
	}

	setCursor(x + 20 + decal, y - 10 + (_height / (nb_lig*2)));
	setTextSize(3);
	print(affi);

	setTextSize(1);
	x = _width / 2;
	setCursor(x + 105, y + 8);// y + 42

	if (p_unite) print(p_unite);

	// print delimiters
	if (p_lig > 1) drawFastHLine(0, _height / nb_lig * (p_lig - 1), _width, 1);
	if (p_lig < nb_lig) drawFastHLine(0, _height / nb_lig * (p_lig), _width, 1);
}


void Vue::cadran(uint8_t p_lig, uint8_t nb_lig, uint8_t p_col, const char *champ, String  affi, const char *p_unite) {

	const int x = _width / 2 * p_col;
	const int y = _height / nb_lig * (p_lig - 1);

	if (champ) {
		setCursor(x + 5 - _width / 2, y + 8);
		setTextSize(1);
		print(champ);
	}

	const int len = affi.length();

	if (len > 6) {
		affi = "---";
	}

	setTextSize(3);

	int16_t x1; int16_t y1; uint16_t w=40; uint16_t h;
	getTextBounds((char*)affi.c_str(), 0, 0, &x1, &y1, &w, &h);

	setCursor(x - 55 + w/2, y - 10 + (_height / (nb_lig*2)));

	printRev(affi);

	setTextSize(1);
	setCursor(x - 4, y + 8);

	if (p_unite) printRev(p_unite);

	// print delimiters
	drawFastVLine(_width / 2, _height / nb_lig * (p_lig - 1), _height / nb_lig, 1);

	if (p_lig > 1)      drawFastHLine(_width * (p_col - 1) / 2, _height / nb_lig * (p_lig - 1), _width / 2, 1);
	if (p_lig < nb_lig) drawFastHLine(_width * (p_col - 1) / 2, _height / nb_lig * p_lig, _width / 2, 1);
}

void Vue::HistoH(uint8_t p_lig, uint8_t nb_lig, sVueHistoConfiguration& h_config_) {

	const int y_base = _height / nb_lig * (p_lig);

	if (h_config_.cur_elem_nb) {

		ASSERT(h_config_.p_f_read);
		ASSERT(h_config_.nb_elem_tot);

		uint16_t dx = _width / h_config_.nb_elem_tot;

		// iterate in buffer
		for (int i=0; i < h_config_.cur_elem_nb; i++) {

			int x_base0 = i * dx;

			tHistoValue elem   = (h_config_.p_f_read)(i);
			tHistoValue height = elem * _height / (nb_lig * h_config_.max_value);

			uint16_t dy = 10;

			if (h_config_.ref_value) {

				if (elem <= h_config_.ref_value) height = h_config_.ref_value * _height / (nb_lig * h_config_.max_value);
				else                             height -= 1;

				dy = abs(elem - h_config_.ref_value) * _height / (nb_lig * h_config_.max_value);

				dy = MAX(dy, 1);

				drawFastPHLine(0, y_base - h_config_.ref_value * _height / (nb_lig * h_config_.max_value), _width, 1);
			}

			// rectangle complet
			this->fillRect(x_base0, y_base-height, dx, dy, 1);
		}
	}

	// print delimiters
	if (p_lig > 1)      drawFastHLine(0, _height / nb_lig * (p_lig - 1), _width, 1);
	if (p_lig < nb_lig) drawFastHLine(0, _height / nb_lig * (p_lig), _width, 1);
}

void Vue::Histo(uint8_t p_lig, uint8_t nb_lig, uint8_t p_col, sVueHistoConfiguration& h_config_) {

	int x_base = _width / 2 * (p_col - 1);
	int y_base = _height / nb_lig * (p_lig);

	if (h_config_.cur_elem_nb) {

		ASSERT(h_config_.p_f_read);
		ASSERT(h_config_.nb_elem_tot);

		int dx = _width / (2 * h_config_.nb_elem_tot);

		// iterate in buffer
		for (int i=0; i < h_config_.cur_elem_nb; i++) {

			int x_base0 = x_base + i * dx;

			tHistoValue elem   = (h_config_.p_f_read)(i);
			tHistoValue height = elem * _height / (nb_lig * h_config_.max_value);

			// petite barre
			this->fillRect(x_base0, y_base-height, dx, 4, 1);
		}
	}

	// print delimiters
	drawFastVLine(_width / 2, _height / nb_lig * (p_lig - 1), _height / nb_lig, 1);

	if (p_lig > 1)      drawFastHLine(_width * (p_col - 1) / 2, _height / nb_lig * (p_lig - 1), _width / 2, 1);
	if (p_lig < nb_lig) drawFastHLine(_width * (p_col - 1) / 2, _height / nb_lig * p_lig, _width / 2, 1);
}

void Vue::cadranRR(uint8_t p_lig, uint8_t nb_lig, uint8_t p_col, const char *champ, RRZone &zone) {

	const int x = _width / 2 * p_col;
	const int y = _height / nb_lig * (p_lig - 1);

	if (champ) {
		setCursor(x + 5 - _width / 2, y + 8);
		setTextSize(1);
		print(champ);
	}

	// Print the data
	setCursor(x - 55 - 20, y - 10 + (_height / (nb_lig*2)));
	setTextSize(3);

	uint32_t cur_zone = zone.getCurBin();
	float val_max = zone.getValMax();

	// force span to be in a window
	val_max = MIN(val_max, 50);
	val_max = MAX(val_max, 5);

	// loop over bins
	for (uint32_t i=0; i< zone.getNbBins(); i++) {

		float val = zone.getValZX(i);

		LOG_DEBUG(">> RR val displayed: %f", val);

		int16_t width = regFenLim(val, 0, val_max, 2, _width / 2 - 35);
		this->fillRect(x - _width / 2 + 20, y + 20 + i*6, width, 4, 1);

		if (i == cur_zone) {
			setCursor(x - _width / 2 + 7, y + 15 + i*6);
			setTextSize(2);
			print((char)('>'));
		}

	}

	setCursor(x - _width / 2 + 40, y + 17 + zone.getNbBins()*6);
	setTextSize(2);
	print((unsigned)zone.getValMax());

	// print delimiters
	drawFastVLine(_width / 2, _height / nb_lig * (p_lig - 1), _height / nb_lig, 1);

	if (p_lig > 1)      drawFastHLine(_width * (p_col - 1) / 2, _height / nb_lig * (p_lig - 1), _width / 2, 1);
	if (p_lig < nb_lig) drawFastHLine(_width * (p_col - 1) / 2, _height / nb_lig * p_lig, _width / 2, 1);

}
