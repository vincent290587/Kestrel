/*
 * Phase 8: replaces stravaV10's libraries/task_manager (a hand-rolled
 * cooperative scheduler -- flagged as "trash, replaced by Zephyr" in the
 * very first architecture survey, back in Phase 1) with Zephyr's own
 * threads + k_event, validated with a real producer/consumer handoff
 * running concurrently on hardware.
 *
 * stravaV10's actual API (task_manager_wrapper.h's w_task_events_wait() /
 * w_task_events_set(task_id, mask), used throughout Locator/GPSMGMT/Boucle*
 * to wake the "Boucle" task on a new GPS fix, ANT+ FE-C update, etc.) is a
 * generic "target any task by ID" indirection over one shared flags word
 * per task. Zephyr's k_event is closer to the metal and doesn't need that
 * indirection: each logical task gets its own k_event object, and a
 * producer just calls k_event_set() on the specific one it means. So this
 * isn't a 1:1 API clone -- it's the idiomatic Zephyr shape of the same
 * behavior. Concretely, Locator's:
 *   w_task_events_set(m_tasks_id.boucle_id, TASK_EVENT_LOCATION);
 * becomes:
 *   k_event_set(&boucle_events, TASK_EVENT_LOCATION);
 * once Locator/Boucle are actually ported (they aren't yet -- this is
 * infrastructure ready for that, demonstrated with a stand-in producer).
 *
 * task_delay()/w_task_delay() has an even simpler replacement: every
 * Zephyr thread already has k_sleep() built in, no custom scheduler tick
 * management (task_manager.c's task_tick_manage()) needed at all.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "task_demo.h"

/* Same bit values as task_manager_wrapper.h's TASK_EVENT_LOCATION (BIT(0)),
 * kept for documentation fidelity even though nothing else references the
 * original header here.
 */
#define TASK_EVENT_LOCATION BIT(0)

static struct k_event boucle_events;

#define STACK_SIZE 1024
#define PRODUCER_PRIO 5
#define CONSUMER_PRIO 5

K_THREAD_STACK_DEFINE(producer_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(consumer_stack, STACK_SIZE);
static struct k_thread producer_thread_data;
static struct k_thread consumer_thread_data;

/* Stand-in for Locator::tasks() calling w_task_events_set() on a new GPS
 * fix -- real rate would be whatever the GPS module's fix interval is
 * (stravaV10 defaults to 1 Hz), used verbatim here since it's realistic
 * and short enough to observe a handful of handoffs in a smoke test.
 */
static void producer_main(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (int i = 0; i < 5; i++) {
		k_sleep(K_SECONDS(1));
		printk("task_demo: producer -> TASK_EVENT_LOCATION (%d/5)\n", i + 1);
		k_event_set(&boucle_events, TASK_EVENT_LOCATION);
	}
}

/* Stand-in for Boucle blocking in w_task_events_wait(TASK_EVENT_LOCATION). */
static void consumer_main(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (int i = 0; i < 5; i++) {
		uint32_t matched = k_event_wait(&boucle_events, TASK_EVENT_LOCATION, false, K_FOREVER);

		/* Same clear-only-what-matched semantic as the original
		 * task_events_wait() (which ANDs the matched bits out of
		 * the task's flags word before returning). */
		k_event_clear(&boucle_events, matched);

		printk("task_demo: consumer <- event 0x%x, processing\n", matched);
	}
}

void task_demo_start(void)
{
	k_event_init(&boucle_events);

	k_thread_create(&producer_thread_data, producer_stack, STACK_SIZE, producer_main, NULL,
			 NULL, NULL, PRODUCER_PRIO, 0, K_NO_WAIT);
	k_thread_create(&consumer_thread_data, consumer_stack, STACK_SIZE, consumer_main, NULL,
			 NULL, NULL, CONSUMER_PRIO, 0, K_NO_WAIT);

	printk("task_demo: producer/consumer threads started\n");
}
