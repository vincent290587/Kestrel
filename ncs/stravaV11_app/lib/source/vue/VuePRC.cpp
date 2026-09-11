/*
 * VuePRC.cpp
 *
 *  Created on: 12 d�c. 2017
 *      Author: Vincent
 *
 * GFX port Phase D: replaced "#include Model.h" with the specific
 * globals needed (vue_global.h, att_global.h, Locator.h for `locator`,
 * SegmentManager.h for `segMngr`). Dropped "ant.h"/"sd/sd_functions.h"
 * -- vestigial, nothing in this file's body uses symbols from either.
 * Dropped sysview_task_void_enter()/exit() SystemView profiling calls
 * (SDK16 tracing, not ported, same as every other file in this port).
 * LS027_PIXEL_BLACK replaced with a literal 0 (opposite polarity under
 * this port's ZephyrGFX convention -- see ZephyrGFX.h's own comment).
 *
 * tasksPRC() is the one real, deliberate trim, not a straight port:
 * stravaV10's original reads `boucle_crs.m_s_parcours` and calls
 * `boucle_crs.needsInit()` -- Boucle*, not ported (the whole reason this
 * UI port is decoupled from it, per the GFX port plan in todo.md).
 * p_parcours stays nullptr here -- the honest, current answer, matching
 * the original's own "if (p_parcours) {...} else { LOG_INFO("No PRC in
 * memory") }" else-branch exactly, just always taking it for now. The
 * needsInit() transition out of eVuePRCScreenInit is also not
 * ported -- without a real Boucle driving it, this stays on the loading
 * screen until LOCATOR_MAX_DATA_AGE_MS naturally applies instead (the
 * *other* transition condition, still real and still ported). Once
 * Boucle*-related segments work resumes (currently descoped -- see CLAUDE.md's
 * 2026-09-07 decision), both real gaps close together: they're the same
 * piece of missing state.
 *
 * afficheParcours()/afficheSegment()/propagateEventsPRC()/
 * displayLoading() are ported unmodified/faithfully -- none of them
 * touch boucle_crs at all, and segMngr (SegmentManager, already ported
 * Phase 1) is a real, live object here, not stubbed.
 */

#include "assert_wrapper.h"
#include <vue/VuePRC.h>
#include <vue/Screenutils.h>
#include "vue_global.h"
#include "att_global.h"
#include "Locator.h"
#include "debug_screen_data.h"
#include "segger_wrapper.h"
#include "utils.h"
#include "g_structs.h"

extern Locator locator;
#if defined(STRAVA_SEGMENTS_ENABLED)
extern SegmentManager segMngr;
#endif
extern sHrmInfo hrm_info;
extern sBscInfo bsc_info;

#define VUE_PRC_NB_LINES         7

VuePRC::VuePRC() : Adafruit_GFX(0, 0) {
	m_prc_screen_mode = eVuePRCScreenInit;

	m_distance_prc = 0.;
}

eVuePRCScreenModes VuePRC::tasksPRC() {

	LOG_DEBUG("Last update age: %lu", locator.getLastUpdateAge());

	// switch back and forth to GPS information page
	if (eVuePRCScreenInit != m_prc_screen_mode &&
			locator.getLastUpdateAge() > LOCATOR_MAX_DATA_AGE_MS) {

		m_prc_screen_mode = eVuePRCScreenGps;
	} else {

		m_prc_screen_mode = eVuePRCScreenDataFull;
	}

	Parcours *p_parcours = nullptr; // see this file's top-of-file note

	switch (m_prc_screen_mode) {
	case eVuePRCScreenInit:
	{
		this->displayLoading();
		break;
	}
	case eVuePRCScreenGps:
	{
		// display GPS page
		this->displayGPS();
		break;
	}
	case eVuePRCScreenDataFull:
	{
		if (p_parcours) {

			LOG_INFO("Printing PRC");

			this->cadran(1, VUE_PRC_NB_LINES, 1, "Dist", _fmkstr(att.dist / 1000.f, 1U), "km");
			this->cadran(1, VUE_PRC_NB_LINES, 2, "Pwr", _imkstr(att.pwr), "W");

			this->cadran(2, VUE_PRC_NB_LINES, 1, "Speed", _fmkstr(att.loc.speed, 1U), "km/h");
			this->cadran(2, VUE_PRC_NB_LINES, 2, "Climb", _imkstr((int)att.climb), "m");

			this->cadran(3, VUE_PRC_NB_LINES, 1, "CAD", _imkstr(bsc_info.cadence), "rpm");
			this->cadran(3, VUE_PRC_NB_LINES, 2, "HRM", _imkstr(hrm_info.bpm), "bpm");

			this->cadran(4, VUE_PRC_NB_LINES, 1, "SL", _imkstr(att.slope), "%");
			this->cadran(4, VUE_PRC_NB_LINES, 2, "VA", _fmkstr(att.vit_asc, 2U), "m/s");

			// display parcours
			this->afficheParcours(5, p_parcours->getListePoints());

#if defined(STRAVA_SEGMENTS_ENABLED)
			// display the segments
			for (int j=0; j < segMngr.getNbSegs() && j < NB_SEG_ON_DISPLAY - 1; j++) {

				ASSERT(segMngr.getSeg(j)->p_seg);

				this->afficheSegment(5, segMngr.getSeg(j)->p_seg);
			}
#endif

		} else {
			LOG_INFO("No PRC in memory");
		}

		float current_ma = 0.f, voltage_v = 0.f;
		bool has_stc = debug_screen_get_stc_current_ma(&current_ma);

		(void)debug_screen_get_stc_voltage_v(&voltage_v);

		this->cadran(7, VUE_PRC_NB_LINES, 1, "Avg", _imkstr((int)current_ma), "mA");
		this->cadran(7, VUE_PRC_NB_LINES, 2, "SOC", has_stc ? _imkstr((int)percentageBatt(voltage_v, current_ma)) : "--", "%");

		break;
	}

	}

	return m_prc_screen_mode;
}

/**
 *
 * @param event
 * @return
 */
bool VuePRC::propagateEventsPRC(eButtonsEvent event) {

	switch (event) {
		case eButtonsEventLeft:
		{
			this->decreaseZoom();
			break;
		}

		case eButtonsEventRight:
		{
			this->increaseZoom();
			break;
		}

		case eButtonsEventCenter:
		default:
		{
			break;
		}
	}

	return true;
}

/**
 *
 * @param ligne
 * @param p_liste
 */
void VuePRC::afficheParcours(uint8_t ligne, ListePoints2D *p_liste) {

	float minLat = 100.;
	float minLon = 400.;
	float maxLat = -100.;
	float maxLon = -400.;
	uint16_t points_nb = 0;
	Point2D pCourant, pSuivant;

	uint16_t debut_cadran = _height / VUE_PRC_NB_LINES * (ligne - 1);
	uint16_t fin_cadran   = _height / VUE_PRC_NB_LINES * (ligne + 1);

	// print delimiters
	if (ligne > 1)                drawFastHLine(0, debut_cadran, _width, 0);
	if (ligne < VUE_PRC_NB_LINES) drawFastHLine(0, fin_cadran,   _width, 0);

	ASSERT(p_liste);

	LOG_DEBUG("Printing PRC");

	// init zoom
	this->setSpan(_width, fin_cadran - debut_cadran);

	float dZoom_h;
	float dZoom_v;

	LOG_INFO("PRC size %d", p_liste->size());

	m_distance_prc = p_liste->dist(att.loc.lat, att.loc.lon);

	this->computeZoom(att.loc.lat, m_distance_prc, dZoom_h, dZoom_v);

	// our pos is at the center
	maxLat = minLat = att.loc.lat;
	maxLon = minLon = att.loc.lon;

	// zoom level
	minLat -= dZoom_v;
	minLon -= dZoom_h;
	maxLat += dZoom_v;
	maxLon += dZoom_h;

	// on affiche
	points_nb = 0;
	uint16_t printed_nb = 0;
	for (auto& pPt : *p_liste->getLPTS()) {

		pSuivant = pPt;

		// print only points in the current zoom
		if (points_nb &&
				(((pCourant._lon > minLon && pCourant._lon < maxLon) &&
				(pCourant._lat > minLat && pCourant._lat < maxLat)) ||
				((pSuivant._lon > minLon && pSuivant._lon < maxLon) &&
				(pSuivant._lat > minLat && pSuivant._lat < maxLat)))) {

			if (!pSuivant.isValid() || !pCourant.isValid()) break;

			int16_t thickness = 1;
			if (points_nb >= p_liste->idx_P1 &&
					points_nb < p_liste->idx_P1 + 30) {
				// show which path to take
				thickness = 2;
			}

			drawLine(regFenLim(pCourant._lon, minLon, maxLon, 0.f, _width),
					regFenLim(pCourant._lat, minLat, maxLat, fin_cadran, debut_cadran),
					regFenLim(pSuivant._lon, minLon, maxLon, 0.f, _width),
					regFenLim(pSuivant._lat, minLat, maxLat, fin_cadran, debut_cadran),
					0, thickness);

			printed_nb++;
		}

		pCourant = pPt;
		points_nb++;
	}

	LOG_INFO("VuePRC %u / %u points printed", printed_nb, points_nb);

	// ma position
	if (att.loc.course > 0) {
		int16_t x0f, x0 = _width/2;
		int16_t y0f, y0 = (debut_cadran+fin_cadran)/2 - 15;

		int16_t x1f, x1 = _width/2 + 5;
		int16_t y1f, y1 = (debut_cadran+fin_cadran)/2 + 5;

		int16_t x2f, x2 = _width/2 - 5;
		int16_t y2f, y2 = (debut_cadran+fin_cadran)/2 + 5;

		rotate_point(att.loc.course, _width/2, (debut_cadran+fin_cadran)/2, x0, y0, x0f, y0f);
		rotate_point(att.loc.course, _width/2, (debut_cadran+fin_cadran)/2, x1, y1, x1f, y1f);
		rotate_point(att.loc.course, _width/2, (debut_cadran+fin_cadran)/2, x2, y2, x2f, y2f);

		drawTriangle(x0f, y0f, x1f, y1f, x2f, y2f, 0);
	} else {
		fillCircle(_width/2, (debut_cadran+fin_cadran)/2, 4, 0);
	}

	// print the zoom level in m
	setTextSize(1);
	setCursor(_width - 40, debut_cadran + 10);
	print((int)this->getLastZoom());
	print("m");
}

#if defined(STRAVA_SEGMENTS_ENABLED)
/**
 *
 * @param ligne
 * @param p_seg
 */
void VuePRC::afficheSegment(uint8_t ligne, Segment *p_seg) {

	float minLat = 100.;
	float minLon = 400.;
	float maxLat = -100.;
	float maxLon = -400.;
	uint16_t points_nb = 0;
	ListePoints *liste = nullptr;
	Point pCourant, pSuivant;
	Point *maPos = nullptr;

	ASSERT(p_seg);

	if (p_seg->longueur() < 4) {
		LOG_ERROR("Segment %s not loaded properly", p_seg->getName());
		return;
	}

	uint16_t debut_cadran = _height / VUE_PRC_NB_LINES * (ligne - 1);
	uint16_t fin_cadran   = _height / VUE_PRC_NB_LINES * (ligne + 1);

	// print delimiters
	if (ligne > 1)                drawFastHLine(0, debut_cadran, _width, 0);
	if (ligne < VUE_PRC_NB_LINES) drawFastHLine(0, fin_cadran, _width, 0);

	// on cherche la taille de fenetre
	liste = p_seg->getListePoints();

	ASSERT(liste);

	LOG_DEBUG("Printing PRC");

	// init zoom
	this->setSpan(_width, fin_cadran - debut_cadran);

	float dZoom_h;
	float dZoom_v;

	this->computeZoom(att.loc.lat, m_distance_prc, dZoom_h, dZoom_v);

	// our pos is at the center
	maxLat = minLat = att.loc.lat;
	maxLon = minLon = att.loc.lon;

	// zoom level
	minLat -= dZoom_v;
	minLon -= dZoom_h;
	maxLat += dZoom_v;
	maxLon += dZoom_h;

	// on affiche
	points_nb = 0;
	uint16_t pourc = 0;
	bool pourc_found = 0;
	for (auto& pPt : *liste->getLPTS()) {

		if (p_seg->getStatus() == SEG_OFF && points_nb > SEG_OFF_NB_POINTS) {
			break;
		}

		pSuivant = pPt;

		if (!pourc_found) {
			if (pPt._rtime == liste->m_P1._rtime) {
				pourc_found = true;
				pourc = (100 * points_nb) / liste->size();
			}
		}

		// print only points in the current zoom
		if (points_nb &&
				(((pCourant._lon > minLon && pCourant._lon < maxLon) &&
						(pCourant._lat > minLat && pCourant._lat < maxLat)) ||
						((pSuivant._lon > minLon && pSuivant._lon < maxLon) &&
								(pSuivant._lat > minLat && pSuivant._lat < maxLat)))) {

			if (!pSuivant.isValid() || !pCourant.isValid()) break;

			drawLine(regFenLim(pCourant._lon, minLon, maxLon, 0.f, _width),
					regFenLim(pCourant._lat, minLat, maxLat, fin_cadran, debut_cadran),
					regFenLim(pSuivant._lon, minLon, maxLon, 0.f, _width),
					regFenLim(pSuivant._lat, minLat, maxLat, fin_cadran, debut_cadran), 0);
		}

		pCourant = pSuivant;
		points_nb++;
	}
	LOG_DEBUG("VuePRC %u points printed", points_nb);

	if (p_seg->getStatus() < SEG_OFF) {

		// draw a rect at the end of the segment (done)
		if (((pSuivant._lon > minLon && pSuivant._lon < maxLon) &&
				(pSuivant._lat > minLat && pSuivant._lat < maxLat))) {

			drawRect(regFenLim(pSuivant._lon, minLon, maxLon, 0.f, _width) - 5,
					regFenLim(pSuivant._lat, minLat, maxLat, fin_cadran, debut_cadran) - 5, 10, 10, 0);
		}
	} else if (p_seg->getStatus() != SEG_OFF) {

		// draw a flag at the end of the segment (not done yet)
		if (((pSuivant._lon > minLon && pSuivant._lon < maxLon) &&
						(pSuivant._lat > minLat && pSuivant._lat < maxLat))) {

			drawRect(regFenLim(pSuivant._lon, minLon, maxLon, 0.f, _width) - 5,
					regFenLim(pSuivant._lat, minLat, maxLat, fin_cadran, debut_cadran) - 5, 10, 10, 0);
			fillRect(regFenLim(pSuivant._lon, minLon, maxLon, 0.f, _width) - 5,
					regFenLim(pSuivant._lat, minLat, maxLat, fin_cadran, debut_cadran) - 5, 5, 5, 0);
			fillRect(regFenLim(pSuivant._lon, minLon, maxLon, 0.f, _width),
					regFenLim(pSuivant._lat, minLat, maxLat, fin_cadran, debut_cadran), 5, 5, 0);
		}

	} else {

		// draw a circle at the start of the segment
		maPos = liste->getFirstPoint();
		if (((maPos->_lon > minLon && maPos->_lon < maxLon) &&
				(maPos->_lat > minLat && maPos->_lat < maxLat)))
			drawCircle(regFenLim(maPos->_lon, minLon, maxLon, 0.f, _width),
					regFenLim(maPos->_lat, minLat, maxLat, fin_cadran, debut_cadran), 5.f, 0);
	}

	// return before printing text
	if (p_seg->getStatus() == SEG_OFF) {
		return;
	}

	setTextSize(2);
	setCursor(_width / 2 + 10, 0.5f * (debut_cadran + fin_cadran) + 10.f);

	print(_fmkstr(p_seg->getAvance(), 1U));

	// completion
	if (pourc_found) {
		if (p_seg->getStatus() < SEG_OFF) {
			pourc = 100;
		}

		setCursor(10, debut_cadran + 10);
		print(pourc);
		print("%");
	}
}
#endif /* STRAVA_SEGMENTS_ENABLED */

void VuePRC::displayLoading() {

	static uint8_t nb_dots = 0;

	vue.setCursor(0, 20);
	vue.setTextSize(3);

	vue.print("Loading");
	for (int i=0; i < nb_dots; i++) vue.print(".");
	vue.println(".");

	nb_dots++;
	nb_dots = nb_dots % 10;
}
