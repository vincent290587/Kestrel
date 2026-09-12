/* Compiled only when ANT_ENABLED is OFF (see CMakeLists.txt) --
 * model_glue.c/ride_recorder.c/menu_content.cpp call these functions
 * unconditionally regardless of whether real ANT+ profile channels
 * exist (they're the live-sensor-data bridge between whatever radio
 * layer is active and the UI/ride-recording logic), so something has to
 * provide these symbols when ant_demo.c/hrm_demo.c/bsc_demo.c/
 * fec_demo.c/ant_dm_demo.c themselves aren't compiled in. Always report
 * "never paired"/stale-forever -- exactly what those callers already
 * treat as the normal "sensor not connected" case, not a special one
 * that needs its own handling.
 */

#include <stdbool.h>
#include <stdint.h>

#include "hrm_demo.h"
#include "bsc_demo.h"
#include "fec_demo.h"
#include "ant_dm_demo.h"

uint8_t hrm_demo_get_bpm(void)
{
	return 0;
}

uint16_t hrm_demo_get_rr_ms(void)
{
	return 0;
}

bool hrm_demo_is_paired(void)
{
	return false;
}

uint32_t hrm_demo_get_age_ms(void)
{
	return UINT32_MAX;
}

uint32_t bsc_demo_get_speed(void)
{
	return 0;
}

uint32_t bsc_demo_get_cadence(void)
{
	return 0;
}

bool bsc_demo_is_paired(void)
{
	return false;
}

uint32_t bsc_demo_get_age_ms(void)
{
	return UINT32_MAX;
}

uint16_t fec_demo_get_power_w(void)
{
	return 0;
}

bool fec_demo_is_paired(void)
{
	return false;
}

uint32_t fec_demo_get_power_age_ms(void)
{
	return UINT32_MAX;
}

uint8_t fec_demo_get_cadence_rpm(void)
{
	return 0;
}

void ant_dm_demo_search_start(enum ant_dm_sensor_type type)
{
	(void)type;
}

void ant_dm_demo_search_list(void)
{
}
