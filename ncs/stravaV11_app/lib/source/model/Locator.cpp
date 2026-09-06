/*
 * Locator.cpp
 *
 *  Created on: 19 oct. 2017
 *      Author: Vincent
 */

#include "segger_wrapper.h"
#include "parameters.h"
#include "nordic_common.h"
#include "utils.h"
#include "TinyGPS++.h"
#include <stdlib.h>
#include <Locator.h>
#include "GPSMGMT.h"

// Normally declared in Model.h (not ported yet); defined in
// adapters/gps_mgmt_stub.cpp.
extern GPS_MGMT gps_mgmt;

#include <vector>

#define NB_SATS_TO_DETAIL           7

static bool m_is_gps_updated = false;

TinyGPSPlus   gps;
TinyGPSCustom hdop(gps, "GPGSA", 16);       // $GPGSA sentence, 16th element
TinyGPSCustom vdop(gps, "GPGSA", 17);       // $GPGSA sentence, 17th element

TinyGPSCustom totalGPGSVMessages(gps, "GPGSV", 1); // $GPGSV sentence, first element
TinyGPSCustom messageNumber(gps, "GPGSV", 2);      // $GPGSV sentence, second element

TinyGPSCustom satsInView(gps, "GPGSV", 3);  // $GPGSV sentence, third element

TinyGPSCustom satsInUse(gps, "GNGGA", 7);   // $GNGGA sentence, 7th element

TinyGPSCustom satNumber[NB_SATS_TO_DETAIL]; // to be initialized later
TinyGPSCustom elevation[NB_SATS_TO_DETAIL];
TinyGPSCustom azimuth[NB_SATS_TO_DETAIL];
TinyGPSCustom snr[NB_SATS_TO_DETAIL];

static std::vector<sSatellite> sats;

/**
 *
 * @param c Character to encode
 * @return
 */
uint32_t locator_encode_char(char c) {

	LOG_DEBUG("%c", c);

	if (gps.encode(c)) {

		if (GPS_SENTENCE_GPRMC == gps.curSentenceType) {
			m_is_gps_updated = true;
		}

	}

	return 0;
}

// locator_dispatch_lns_update() (BLE LNS -> nrf_loc) isn't ported: ble_lns_c
// is out of scope for this port (user decision, see CLAUDE.md), so nothing
// ever produces LNS updates to dispatch.

Locator::Locator() {

	m_is_gps_updated = false;

	anyChanges   = false;

	m_nb_nrf_pos = 0;
	m_nb_sats    = 0;
}


void Locator::init(void) {

	// Initialize all the uninitialized TinyGPSCustom objects
	for (int i = 0; i < NB_SATS_TO_DETAIL; ++i)
	{
	    satNumber[i].begin(gps, "GPGSV", 4 + 4 * i); // offsets 4, 8, 12, 16
	    elevation[i].begin(gps, "GPGSV", 5 + 4 * i); // offsets 5, 9, 13, 17
	    azimuth[i].begin(  gps, "GPGSV", 6 + 4 * i); // offsets 6, 10, 14, 18
	    snr[i].begin(      gps, "GPGSV", 7 + 4 * i); // offsets 7, 11, 15, 19
	}

}

/**
 *
 * @return
 */
eLocationSource Locator::getUpdateSource() {

	this->tasks();

	if (sim_loc.isUpdated()) {
		return eLocationSourceSIM;
	} else if (sim_loc.getAge() < 2000) {
		return eLocationSourceNone;
	}

	if (gps_loc.isUpdated()) {
		return eLocationSourceGPS;
	} else if (gps_loc.getAge() < 1500) {
		return eLocationSourceNone;
	}

	if (nrf_loc.isUpdated() && !gps_mgmt.isFix()) {
		return eLocationSourceNRF;
	} else if (nrf_loc.isUpdated()) {
		LOG_INFO("LNS data refused: GPS data valid");
	}

	return eLocationSourceNone;
}

/**
 *
 * @return
 */
bool Locator::isUpdated()      {

	eLocationSource source = this->getUpdateSource();

	if (eLocationSourceNone != source) {
		LOG_DEBUG("Locator source: %u", source);
		return true;
	}
	return false;
}



/**
 *
 * @return The last update age in milliseconds
 */
uint32_t Locator::getLastUpdateAge() {

	uint32_t last_update_age;

	last_update_age = MIN(sim_loc.getAge(), gps_loc.getAge());
	last_update_age = MIN(last_update_age , nrf_loc.getAge());

	return last_update_age;
}

/**
 *
 * @return The last GPGSV sentence age
 */
uint32_t Locator::getUsedSatsAge() {

	return satsInUse.age();
}

/**
 *
 * @param lat
 * @param lon
 * @param sec_
 * @return
 */
eLocationSource Locator::getPosition(SLoc& loc_, SDate& date_) {

	eLocationSource res = this->getUpdateSource();

	LOG_INFO("Locator update source: %u", (uint8_t)res);

	switch (res) {
	case eLocationSourceSIM:
	{
		loc_.lat = sim_loc.data.lat;
		loc_.lon = sim_loc.data.lon;
		loc_.alt = sim_loc.data.alt;
		loc_.speed = sim_loc.data.speed;
		loc_.course = -1.f;
		date_.secj = sim_loc.data.utc_time;
		date_.date = sim_loc.data.date;
		date_.timestamp = sim_loc.data.utc_timestamp;
		sim_loc.clearIsUpdated();
	}
	break;
	case eLocationSourceNRF:
	{
		loc_.lat = nrf_loc.data.lat;
		loc_.lon = nrf_loc.data.lon;
		loc_.alt = nrf_loc.data.alt;
		loc_.speed = nrf_loc.data.speed;
		loc_.course = nrf_loc.data.course;
		date_.secj = nrf_loc.data.utc_time;
		date_.date = nrf_loc.data.date;
		date_.timestamp = nrf_loc.data.utc_timestamp;
		nrf_loc.clearIsUpdated();

		if (5 == ++m_nb_nrf_pos) {

			gps_mgmt.startHostAidingEPO(nrf_loc.data, 500);

		}
	}
	break;
	case eLocationSourceGPS:
	{
		loc_.lat = gps_loc.data.lat;
		loc_.lon = gps_loc.data.lon;
		loc_.alt = gps_loc.data.alt;
		loc_.speed = gps_loc.data.speed;
		loc_.course = gps_loc.data.course;
		date_.secj = gps_loc.data.utc_time;
		date_.date = gps_loc.data.date;
		date_.timestamp = gps_loc.data.utc_timestamp;
		gps_loc.clearIsUpdated();

		m_nb_nrf_pos = 0;
	}
	break;
	case eLocationSourceNone:
		date_.secj = gps_loc.data.utc_time;
		date_.date = gps_loc.data.date;
		date_.timestamp = gps_loc.data.utc_timestamp;
		break;
	default:
		break;
	}

	return res;
}


/**
 *
 * @param lat
 * @param lon
 * @param sec_
 * @return
 */
eLocationSource Locator::getDate(SDate& date_) {

	eLocationSource res = this->getUpdateSource();

	switch (res) {
	case eLocationSourceSIM:
	{
		date_.secj = sim_loc.data.utc_time;
		date_.date = 291217;

		date_.timestamp = millis();
	}
	break;
	case eLocationSourceNRF:
	{
		date_.secj = nrf_loc.data.utc_time;
		date_.date = nrf_loc.data.date;

		date_.timestamp = nrf_loc.data.utc_timestamp;

	}
	break;
	case eLocationSourceGPS:
	{
		date_.secj = gps_loc.data.utc_time;
		date_.date = gps_loc.data.date;

		date_.timestamp = gps_loc.data.utc_timestamp;
	}
	break;
	case eLocationSourceNone:
		date_.secj = gps_loc.data.utc_time;
		date_.date = gps_loc.data.date;

		date_.timestamp = gps_loc.data.utc_timestamp;
		break;
	default:
		break;
	}

	return res;
}

/**
 * Used to get the data from the GPS parsing module
 */
void Locator::tasks() {

	if (m_is_gps_updated) {
		m_is_gps_updated = false;

		if (gps.time.isValid()) {
			gps_loc.data.utc_time = get_sec_jour(gps.time.hour(), gps.time.minute(), gps.time.second());

			gps_loc.data.utc_timestamp = millis();

			gps_loc.data.date = gps.date.year()   % 100;
			gps_loc.data.date += gps.date.day()   * 10000;
			gps_loc.data.date += gps.date.month() * 100;

		}

		if (gps.location.isValid()) {

			gps_loc.data.speed  = (float)gps.speed.kmph();
			gps_loc.data.alt    = (float)gps.altitude.meters();
			gps_loc.data.lat    = (float)gps.location.lat();
			gps_loc.data.lon    = (float)gps.location.lng();

			gps_loc.data.course = (float)gps.course.deg();

			LOG_INFO("GPS location set");

			gps_loc.setIsUpdated();

		} else if (gps.time.isValid()) {
			LOG_DEBUG("GPS location invalid !");
			// trick to force taking LNS data
			gps_loc.data.utc_time -= (LNS_OVER_GPS_DTIME_S + 3);
		}

		if (totalGPGSVMessages.isUpdated()) {

			// clear it
			(void)totalGPGSVMessages.value();
			sats.clear();

			for (int i=0; i < NB_SATS_TO_DETAIL; ++i) {

				int no = atoi(satNumber[i].value());

				if (no && elevation[i].isUpdated()) {

					sSatellite sat_ = {
							.active    = ACTIVE_VAL,
							.elevation = atoi(elevation[i].value()),
							.azimuth   = atoi(azimuth[i].value()),
							.snr       = atoi(snr[i].value()),
							.no        = no,
					};
					sats.push_back(sat_);
				}


			}
		}

	}

}

/**
 * Returns the GPS date
 *
 * @param iYr
 * @param iMo
 * @param iDay
 * @param iHr
 */
bool Locator::getGPSDate(int& iYr, int& iMo, int& iDay, int& iHr) {

	iYr = gps.date.year();
	iMo = gps.date.month();
	iDay = gps.date.day();
	iHr = gps.time.hour();

	return (gps.date.isValid() && gps.time.isValid());
}


/**
 *
 */
// displayGPS2() (raw LS027/vue screen dump) isn't ported -- it's UI drawing,
// not GPS logic. Belongs with the Phase 7 UI port instead.

