/*
 * STC3100 fuel-gauge/coulomb-counter polling, ported from stravaV10's
 * STC3100::init()/refresh() -- the Zephyr I2C plumbing half; stc3100.c
 * holds the hardware-agnostic register decode (see its own top-of-file
 * note for the full "why no upstream driver" background).
 *
 * Same bus/address main.c's stc3100_power_latch_hold() already talks to
 * (arduino_i2c, 0x70) -- that function stays untouched (it's boot-
 * critical: the board loses its own regulator latch if MODE_RUN isn't
 * held from very early in main(), see main.c's own top-of-file note), so
 * this file only refines the already-held MODE_RUN to also include the
 * resolution bits stravaV10's real init() used (0x10|0x02=0x12, not just
 * 0x10) and then starts periodic full-register reads. Never clears
 * MODE_RUN itself, so the latch write here can't undo the one that's
 * already keeping the board powered.
 *
 * Not ported: stravaV10's getAverageCurrent()/resetCharge() (average
 * current since a caller-chosen reset point) -- no caller needs that yet
 * (Model.cpp/UserSettings aren't wired into stravaV11_fw), and
 * stravaV10's soft-reset-after-5s-uptime behavior in refresh() (this
 * demo's poll loop runs indefinitely from boot with no need to
 * distinguish a "just booted" transient the way stravaV10's UI-driven
 * lifecycle did).
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "stc3100.h"
#include "stc3100_demo.h"

LOG_MODULE_REGISTER(stc3100_demo, LOG_LEVEL_INF);

#define STC3100_POLL_MS 1000
#define STC3100_R_SENS_MOHM 100 /* STC3100_CUR_SENS_RES_MO in stravaV10's parameters.h */

static bool m_have_reading;
static struct stc3100_reading m_reading;
static float m_percent;
static int64_t m_last_update_ms; /* 0 = never (see stc3100_demo_get_age_ms()) */

static void stc3100_poll_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(stc3100_poll_work, stc3100_poll_work_handler);

static void stc3100_poll_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	const struct device *i2c = DEVICE_DT_GET(DT_NODELABEL(arduino_i2c));

	uint8_t reg = STC3100_REG_CHARGE_LOW;
	struct stc3100_raw raw;

	int err = i2c_write_read(i2c, STC3100_I2C_ADDR, &reg, 1, &raw, sizeof(raw));

	if (!err) {
		stc3100_decode(&raw, STC3100_R_SENS_MOHM, &m_reading);
		m_percent = stc3100_percent(m_reading.voltage_v, m_reading.current_ma);
		m_have_reading = true;
		m_last_update_ms = k_uptime_get();
	}
	/* No STC3100 on this bus (e.g. the DK): i2c_write_read() fails
	 * cleanly every cycle, getters keep returning false -- same
	 * "clean failure, not a hang" tier as every other un-attached
	 * peripheral in this port. */

	k_work_schedule(&stc3100_poll_work, K_MSEC(STC3100_POLL_MS));
}

void stc3100_demo_start(void)
{
	const struct device *i2c = DEVICE_DT_GET(DT_NODELABEL(arduino_i2c));

	uint8_t mode_cmd[2] = {STC3100_REG_MODE, STC3100_MODE_RUN | STC3100_MODE_HIGHRES};
	int err = i2c_write(i2c, mode_cmd, sizeof(mode_cmd), STC3100_I2C_ADDR);

	if (err) {
		LOG_WRN("stc3100_demo: mode write failed: %d (no STC3100 on this bus?)", err);
	} else {
		LOG_INF("stc3100_demo: mode set to RUN|HIGHRES, starting 1Hz poll");
	}

	k_work_schedule(&stc3100_poll_work, K_NO_WAIT);
}

bool stc3100_demo_get_voltage_v(float *voltage_v)
{
	if (!m_have_reading) {
		return false;
	}
	*voltage_v = m_reading.voltage_v;
	return true;
}

bool stc3100_demo_get_current_ma(float *current_ma)
{
	if (!m_have_reading) {
		return false;
	}
	*current_ma = m_reading.current_ma;
	return true;
}

bool stc3100_demo_get_charge_mah(float *charge_mah)
{
	if (!m_have_reading) {
		return false;
	}
	*charge_mah = m_reading.charge_mah;
	return true;
}

bool stc3100_demo_get_percent(float *percent)
{
	if (!m_have_reading) {
		return false;
	}
	*percent = m_percent;
	return true;
}

uint32_t stc3100_demo_get_age_ms(void)
{
	if (m_last_update_ms == 0) {
		return UINT32_MAX;
	}

	return (uint32_t)(k_uptime_get() - m_last_update_ms);
}

void stc3100_demo_log_reading(void)
{
	if (!m_have_reading) {
		LOG_INF("stc3100: no reading yet");
		return;
	}

	LOG_INF("stc3100: voltage=%d mV current=%d uA charge=%d uAh percent=%d%%",
		(int)(m_reading.voltage_v * 1000), (int)(m_reading.current_ma * 1000),
		(int)(m_reading.charge_mah * 1000), (int)m_percent);
}
