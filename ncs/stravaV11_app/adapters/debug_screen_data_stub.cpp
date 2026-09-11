/*
 * Placeholder for stravaV11_fw's real stc3100_demo-backed
 * debug_screen_data.cpp -- no STC3100 chip (or any I2C bus) exists on
 * native_sim. Same "always fails cleanly" convention as fram_stub.c.
 * .cpp (not .c) so it can read the real global `mes_segments`
 * (ListeSegments, source/routes/Segment.h -- ported, Phase 1) for
 * debug_screen_get_segment_count(); that part isn't a stub, it's the
 * genuine count, just always 0 for now since nothing populates
 * mes_segments yet.
 */

#include "debug_screen_data.h"
#if defined(STRAVA_SEGMENTS_ENABLED)
#include "Segment.h"

extern ListeSegments mes_segments;
#endif

bool debug_screen_get_stc_current_ma(float *current_ma)
{
	(void)current_ma;
	return false;
}

bool debug_screen_get_stc_voltage_v(float *voltage_v)
{
	(void)voltage_v;
	return false;
}

uint32_t debug_screen_get_segment_count(void)
{
#if defined(STRAVA_SEGMENTS_ENABLED)
	return (uint32_t)mes_segments.size();
#else
	return 0;
#endif
}
