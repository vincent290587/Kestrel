/*
 * Phase 8: replaces source/scheduling/power_scheduler.cpp (idle-timeout
 * auto-shutdown -- stravaV10 powers off after 15 minutes with no CRS/FEC
 * activity, POWER_SCHEDULER_MAX_IDLE_MIN) with a Zephyr thread doing the
 * same k_uptime-based idle check power_scheduler__run() did every main
 * loop tick.
 *
 * The idle timeout here is deliberately 4s, not 15 minutes -- this is a
 * smoke test that needs to actually fire within the session, not stravaV10's
 * real threshold (that constant belongs at the call site once Boucle is
 * ported, not in this replacement's logic).
 *
 * On real shutdown, stravaV10 either drives a fuel-gauge chip's shutdown
 * command (STC3100, PROTO_V11 hardware -- see Phase 3 notes on why that
 * driver isn't ported) or a KILL_PIN GPIO. Zephyr's own equivalent is
 * sys_poweroff() (zephyr/sys/poweroff.h) -- nRF52 series has
 * `select HAS_POWEROFF`, so it's genuinely available, but this demo does
 * NOT call it: it would actually power off the DK, ending the debug
 * session with no wired-up wake source to bring it back. The demo logs a
 * stand-in message instead; wiring the real call is a one-line change once
 * this is actually integrated with a ported Boucle.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "power_demo.h"

#define IDLE_TIMEOUT_MS 4000
#define CHECK_PERIOD_MS 500

#define STACK_SIZE 1024
#define PRIO 10 /* low priority, matches power_scheduler's "housekeeping" role */

K_THREAD_STACK_DEFINE(power_stack, STACK_SIZE);
static struct k_thread power_thread_data;

static volatile int64_t last_ping;

void power_demo_ping(void)
{
	last_ping = k_uptime_get();
}

static void power_main(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	last_ping = k_uptime_get();

	while (1) {
		k_sleep(K_MSEC(CHECK_PERIOD_MS));

		int64_t idle_ms = k_uptime_get() - last_ping;

		if (idle_ms > IDLE_TIMEOUT_MS) {
			printk("power_demo: idle %lld ms > %d ms -- would call "
			       "sys_poweroff() here (not actually called, see "
			       "task_demo.c comment)\n",
			       idle_ms, IDLE_TIMEOUT_MS);
			return;
		}
	}
}

void power_demo_start(void)
{
	k_thread_create(&power_thread_data, power_stack, STACK_SIZE, power_main, NULL, NULL, NULL,
			 PRIO, 0, K_NO_WAIT);
	printk("power_demo: idle-timeout thread started (%d ms timeout)\n", IDLE_TIMEOUT_MS);
}
