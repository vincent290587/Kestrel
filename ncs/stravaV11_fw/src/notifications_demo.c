/*
 * Wires notifications.c (ported unmodified from stravaV10, see
 * lib/source/display/notifications.c) into a real Zephyr periodic work
 * item -- the direct replacement for Model.cpp's peripherals_task calling
 * notifications_tasks() every 50ms (source/parameters.h's cadence for
 * that task). notifications_init() + the boot-time red flash mirror
 * stravaV10's own main.cpp:476/506-508 exactly.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "notifications.h"
#include "notifications_demo.h"

#define NOTIFICATIONS_TASK_PERIOD_MS 50 /* Model.cpp's peripherals_task cadence */

static void notifications_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(notifications_work, notifications_work_handler);

static void notifications_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	notifications_tasks();

	k_work_schedule(&notifications_work, K_MSEC(NOTIFICATIONS_TASK_PERIOD_MS));
}

void notifications_demo_start(void)
{
	notifications_init(0); /* pin is fixed by devicetree, see drv_ws2812.h */

	sNeopixelOrders boot_flash;

	SET_NEO_EVENT_RED(boot_flash, eNeoEventNotify, 0);
	notifications_setNotify(&boot_flash);

	printk("notifications: init + boot flash queued, polling every %d ms\n",
	       NOTIFICATIONS_TASK_PERIOD_MS);

	k_work_schedule(&notifications_work, K_NO_WAIT);
}
