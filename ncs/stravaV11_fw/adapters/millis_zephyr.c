/*
 * Native Zephyr implementation of libraries/utils/millis.h. stravaV10's own
 * millis.c/millis_tdd.c are both hardware/scheduler-coupled (app_timer, or
 * the TDD task_scheduler we're deliberately not reintroducing -- see
 * CLAUDE.md, task_manager is "trash, replaced by Zephyr" from the very
 * first architecture survey). Zephyr's own uptime counter replaces it
 * outright rather than bridging through either old implementation.
 */

#include "millis.h"

#include <zephyr/kernel.h>

void millis_init(void)
{
}

uint32_t millis(void)
{
	return (uint32_t)k_uptime_get();
}

void delay_ms(uint32_t delay_)
{
	k_msleep(delay_);
}

void delay_us(uint32_t delay_)
{
	k_busy_wait(delay_);
}
