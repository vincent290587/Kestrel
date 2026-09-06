/*
 * Placeholder for source/sensors/GPSMGMT.cpp (not ported yet -- it's a
 * hardware/UART/task-orchestration layer, same category as Model.cpp and
 * Boucle*.cpp). Locator only calls three GPS_MGMT methods; those are
 * stubbed here so the logic-only port links standalone. Real GPS module
 * bring-up (UART, reset/standby GPIOs) is the stravaV11_fw hardware tier.
 */

#include "GPSMGMT.h"

GPS_MGMT::GPS_MGMT()
{
	m_power_state = eGPSMgmtStateInit;
	m_is_stdby = false;
	m_epo_packet_ind = 0;
	m_epo_packet_nb = 0;
}

bool GPS_MGMT::isFix(void)
{
	return false;
}

void GPS_MGMT::startHostAidingEPO(sLocationData &loc_data, uint32_t age_)
{
	(void)loc_data;
	(void)age_;
}

GPS_MGMT gps_mgmt;
