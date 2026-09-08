/*
 * Locator.h
 *
 *  Created on: 19 oct. 2017
 *      Author: Vincent
 */

#ifndef SOURCE_MODEL_LOCATOR_H_
#define SOURCE_MODEL_LOCATOR_H_

#include <stdint.h>
#include <stdbool.h>
#include "g_structs.h"

#define MAX_SATELLITES     40
#define ACTIVE_VAL         5

typedef struct {
	float lat;
	float lon;
	float alt;
	float speed;
	float course;
} SLoc;

typedef struct {
	float gps_ele;
	float baro_ele;
	float baro_corr;
	float filt_ele;
	float alpha_bar;
	float alpha_zero;
	float climb;
	float vit_asc;
	float rough[3];
	float b_rough;
} SEle;

typedef struct {
	uint8_t bpm;
	uint32_t cadence;
	int16_t pwr;
} SSensors;

typedef struct {
	uint32_t secj;
	uint32_t date;
	uint32_t timestamp;
} SDate;

typedef struct
{
	int active;
	int elevation;
	int azimuth;
	int snr;
	int no;
} sSatellite;

typedef enum {
	eLocationSourceNone,
	eLocationSourceSIM,
	eLocationSourceNRF,
	eLocationSourceGPS,
} eLocationSource;

typedef struct {
	float lat;
	float lon;
	float alt;
	float speed;
	float course;
	uint32_t utc_time;
	uint32_t utc_timestamp;
	uint32_t date;
} sLocationData;

#if defined(__cplusplus)
extern "C" {
#endif /* _cplusplus */

uint32_t locator_encode_char(char c);

// locator_dispatch_lns_update() not ported -- ble_lns_c is out of scope
// for this port (see CLAUDE.md).

#if defined(__cplusplus)
}

#include "Sensor.h"
// Attitude.h was an unused include -- Locator doesn't reference Attitude::
// anything (Attitude.cpp needs the fxos sensor driver, not ported yet).

/**
 *
 */
class Locator {
public:
	Locator();
	void init();
	void tasks();

	// GFX port Phase D: displayGPS2() ported (see Locator.cpp) -- its full
	// data dependency (satsInView/satsInUse/sats[]/gps/gps_mgmt) turned
	// out to already be present in this file since Phase 6 (needed by
	// other already-ported methods), so restoring the one UI-drawing
	// method that used them was low-risk, not the blocker it looked like
	// when first deferred.
	void displayGPS2(void);

	bool getGPSDate(int &iYr, int &iMo, int &iDay, int &iHr);

	eLocationSource getDate(SDate& date_);
	eLocationSource getPosition(SLoc& loc_, SDate& date_);

	eLocationSource getUpdateSource();

	bool isUpdated();

	uint32_t getLastUpdateAge();
	uint32_t getUsedSatsAge();

	Sensor<sLocationData> nrf_loc;
	Sensor<sLocationData> sim_loc;
	Sensor<sLocationData> gps_loc;

private:
	bool anyChanges;

	uint16_t m_nb_nrf_pos;
	uint16_t m_nb_sats;
};

#endif /* _cplusplus */
#endif /* SOURCE_MODEL_LOCATOR_H_ */
