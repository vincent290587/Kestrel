/*
 * GFX port Phase D: real wiring for VueDebug.cpp's debug_screen_data.h --
 * see that header's own comment for why this isn't routed through
 * model_glue.h. Just forwards to stc3100_demo's own getters (Phase 3/11),
 * already validated on real hardware.
 */

#include "debug_screen_data.h"
#include "stc3100_demo.h"

bool debug_screen_get_stc_current_ma(float *current_ma)
{
	return stc3100_demo_get_current_ma(current_ma);
}

bool debug_screen_get_stc_voltage_v(float *voltage_v)
{
	return stc3100_demo_get_voltage_v(voltage_v);
}

/* ListeSegments/Segment.h (source/routes/*) was only ever ported into
 * stravaV11_app (Phase 1), not stravaV11_fw -- see debug_screen_data.h's
 * own comment. Always 0 here, same honest-not-fake reasoning as
 * stravaV11_app's own current answer (nothing populates real segment
 * data in either app yet). */
uint32_t debug_screen_get_segment_count(void)
{
	return 0;
}
