/*
 * Placeholder for the WS2812 LED notification driver (drivers/drv_ws2812.c +
 * source/display/notifications.c in stravaV10), not ported yet. SegmentManager
 * calls notifications_segNotify() to drive the status LED; until the LED
 * driver phase, this just no-ops so the business-logic layer links standalone.
 */

#include "notifications.h"

void notifications_segNotify(neo_sb_seg_params *orders)
{
	(void)orders;
}
