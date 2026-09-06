/*
 * Phase 8: replaces source/scheduling/i2c_scheduler.cpp. Despite the name,
 * that file isn't a generic I2C transaction queue -- it's specifically the
 * periodic sensor-polling orchestrator (fxos/fram/veml init + a
 * BARO_REFRESH_PER_MS-period timer re-triggering baro reads), tightly
 * coupled to Model.h and the not-yet-ported sensor drivers.
 *
 * The actual "queue I2C transactions and poll periodically" need is
 * already superseded by two things done in earlier phases: Zephyr's I2C
 * driver handles transaction queuing/locking internally (Phase 3 just
 * calls i2c_read()/sensor_sample_fetch() directly, no custom queue), and
 * "periodic" is just k_work_delayable re-submitting itself -- which this
 * file demonstrates concretely against the real bme280 device from Phase 3,
 * at stravaV10's actual BARO_REFRESH_PER_MS (100ms, source/parameters.h).
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/printk.h>

#include "poll_demo.h"

#define BARO_REFRESH_PER_MS 100 /* stravaV10's source/parameters.h value */
#define POLL_COUNT_LIMIT 5      /* smoke test: stop after a few cycles, not forever */

static void poll_work_handler(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(poll_work, poll_work_handler);
static int poll_count;

static void poll_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	const struct device *bme280 = DEVICE_DT_GET(DT_NODELABEL(bme280));

	poll_count++;

	if (device_is_ready(bme280)) {
		int err = sensor_sample_fetch(bme280);

		printk("poll_demo: baro poll %d/%d, sensor_sample_fetch() -> %d\n", poll_count,
		       POLL_COUNT_LIMIT, err);
	} else {
		printk("poll_demo: baro poll %d/%d, device not ready (expected)\n", poll_count,
		       POLL_COUNT_LIMIT);
	}

	if (poll_count < POLL_COUNT_LIMIT) {
		k_work_schedule(&poll_work, K_MSEC(BARO_REFRESH_PER_MS));
	}
}

void poll_demo_start(void)
{
	printk("poll_demo: starting periodic baro poll every %d ms\n", BARO_REFRESH_PER_MS);
	k_work_schedule(&poll_work, K_MSEC(BARO_REFRESH_PER_MS));
}
