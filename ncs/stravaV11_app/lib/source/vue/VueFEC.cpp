/*
 * VueFEC.cpp
 *
 *  Created on: 12 dec. 2017
 *      Author: Vincent
 *
 * GFX port Phase D: replaced "#include Model.h" with the specific
 * globals this file needs (vue_global.h for `vue`/addNotif(),
 * Screenutils.h for the *mkstr() helpers). Dropped two things that were
 * dead code in the original too, not just unported:
 *  - _vue_fec_pw_rb_read() and the #if 0 HistoH() block that used it
 *    (lines 70-78 of the original) -- already #if 0'd out in stravaV10
 *    itself, so boucle_fec (Boucle*, not ported) was never actually a
 *    live dependency of this file even before this port.
 *  - The #if 0 STC current/SOC block at the end of tasksFEC() -- also
 *    already disabled in the original; VueGPS.cpp/VueDebug.cpp already
 *    cover that same STC readout via debug_screen_data.h if it's ever
 *    wanted here for real.
 * "ant.h"/"fec.h" were also dropped: nothing in this file actually uses
 * symbols from either (fec_info's type, sFecInfo, comes from
 * g_structs.h) -- vestigial includes, not a real dependency.
 * LS027_PIXEL_BLACK (stravaV10's own LCD driver constant, value 1 there)
 * replaced with a literal 0 -- this port's ZephyrGFX convention is the
 * opposite polarity (1=white, 0=black; see ZephyrGFX.h's own comment).
 */

#include "assert_wrapper.h"
#include <vue/VueFEC.h>
#include <vue/Screenutils.h>
#include "vue_global.h"
#include "segger_wrapper.h"
#include "PowerZone.h"
#include "SufferScore.h"
#include "utils.h"

extern sHrmInfo hrm_info;
extern sBscInfo bsc_info;
extern sFecInfo fec_info;
extern PowerZone zPower;
extern RRZone rrZones;
extern SufferScore suffer_score;
extern sPowerVector powerVector;

#define VUE_FEC_NB_LINES            6

VueFEC::VueFEC() : Adafruit_GFX(0, 0) {

	m_fec_screen_mode = eVueFECScreenInit;
}

eVueFECScreenModes VueFEC::tasksFEC() {

	eVueFECScreenModes res = m_fec_screen_mode;

	if (m_fec_screen_mode == eVueFECScreenInit) {

		// Init with welcome text
		this->setCursor(10,50);
		this->setTextSize(3);
		this->print(String("Connecting"));

		LOG_INFO("VueFEC waiting for sensors");

		if (fec_info.el_time) {
			// FEC just became active
			m_fec_screen_mode = eVueFECScreenDataFull;
			return m_fec_screen_mode;
		}

		vue.addNotif("FEC", "Connecting...", 5, eNotificationTypeComplete);

	} else if (m_fec_screen_mode == eVueFECScreenDataFull) {

		LOG_INFO("VueFEC update full data");

		this->cadranH(1, VUE_FEC_NB_LINES, "Time", _secjmkstr(fec_info.el_time, ':'), NULL);

		this->cadran(2, VUE_FEC_NB_LINES, 1, "CAD", _imkstr(bsc_info.cadence), "rpm");
		this->cadran(2, VUE_FEC_NB_LINES, 2, "HRM", _imkstr(hrm_info.bpm), "bpm");

		this->cadran(3, VUE_FEC_NB_LINES, 1, "Score", _fmkstr(suffer_score.getScore(), 1U), NULL);
		this->cadranZones(3, VUE_FEC_NB_LINES, 2, "PZone", zPower);

		this->cadran(4, VUE_FEC_NB_LINES, 1, "Pwr", _imkstr(fec_info.power), "W");
		this->cadranRR(4, VUE_FEC_NB_LINES, 2, "RR", rrZones);

		this->cadranPowerVector(5, VUE_FEC_NB_LINES, NULL, powerVector);
	}

	return res;
}

// Unit is in newton/meter with a resolution of 1/32
#define SCALE_TORQUE(X, Y)         (((X) * 80) / (Y))     /* 4 LSB is one pixel */

void VueFEC::cadranPowerVector(uint8_t p_lig, uint8_t nb_lig, const char *champ, sPowerVector &vector) {

	// center coordinates
	const int16_t xc = _width / 2;
	const int16_t yc = _height / nb_lig * p_lig;

	// calculate current max torque for scale
	uint8_t trq_ind = 0;
	int16_t arr_torque[5] = { 350 };
	for (int i=0; i < vector.array_size; i++) {

		if (vector.inst_torque_mag_array[i] > arr_torque[trq_ind]) {
			arr_torque[trq_ind] = vector.inst_torque_mag_array[i];
		}
	}

	if (++trq_ind >= sizeof(arr_torque) / sizeof(arr_torque[0])) {
		trq_ind = 0;
	}

	// 5 seconds max moving average
	int16_t max_torque = arr_torque[0];
	for (uint32_t i=1; i < sizeof(arr_torque) / sizeof(arr_torque[0]); i++) {

		if (max_torque < arr_torque[i]) {
			max_torque = arr_torque[i];
		}
	}

	// empty array
	if (!vector.array_size) {
		return;
	}

	// first point coordinates
	int16_t x1;
	int16_t y1;
	int16_t ppower = vector.inst_torque_mag_array[0];
	float first_angle = (float)vector.first_crank_angle;
	rotate_point(first_angle,
			xc, yc,
			xc, yc - SCALE_TORQUE(ppower, max_torque),
			x1, y1);

	int16_t xp, yp;
	int16_t x2=x1, y2=y1;
	for (int i=1; i < vector.array_size; i++) {

		int16_t ppower = vector.inst_torque_mag_array[i];
		int16_t y2 = yc - SCALE_TORQUE(ppower, max_torque);
		float angle = (first_angle + i * 360.f / vector.array_size);
		rotate_point(angle,
				xc, yc,
				xc, y2,
				xp, yp);

		this->drawLine(x1, y1, xp, yp, 0, 3);

		x1 = xp;
		y1 = yp;
	}

	// Print the data
	const int y = _height / nb_lig * (p_lig - 1);
	this->setCursor(0, y - 20 + (_height / (nb_lig*2)));
	this->setTextSize(2);
	this->print(" ");this->println(max_torque);
	this->print(" ");this->println(vector.first_crank_angle);
	this->print(" ");this->println(vector.array_size);

	// close the figure
	this->drawLine(xp, yp, x2, y2, 0, 3);

}

void VueFEC::cadranZones(uint8_t p_lig, uint8_t nb_lig, uint8_t p_col, const char *champ, BinnedData &data) {

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

	uint32_t tot = data.getTimeMax();
	uint32_t cur_zone = data.getCurBin();
	LOG_INFO("PZ time %u", tot);

	// loop over bins
	for (uint32_t i=0; i< data.getNbBins(); i++) {

		int16_t width = regFenLim((float)data.getTimeZX(i), 0.f, tot, 2.f, _width / 2.f - 35.f);
		this->fillRect(x - _width / 2 + 20, y + 20 + i*6, width, 4, 1);

		if (i == cur_zone) {
			setCursor(x - _width / 2 + 7, y + 15 + i*6);
			setTextSize(2);
			print((char)('>'));
		}
	}


	// print delimiters
	drawFastVLine(_width / 2, _height / nb_lig * (p_lig - 1), _height / nb_lig, 1);

	if (p_lig > 1)      drawFastHLine(_width * (p_col - 1) / 2, _height / nb_lig * (p_lig - 1), _width / 2, 1);
	if (p_lig < nb_lig) drawFastHLine(_width * (p_col - 1) / 2, _height / nb_lig * p_lig, _width / 2, 1);

}
