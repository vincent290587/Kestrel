/*
 * STC3100 fuel-gauge/coulomb-counter register decode, ported from
 * stravaV10's source/sensors/STC3100.cpp -- the pure-math half only
 * (computeVoltage()/computeCharge()/computeCurrent()/computeTemp()/
 * computeCounter()). No Zephyr sensor driver exists for this chip
 * (Zephyr's own sbs_gauge targets a different, SBS-compliant protocol
 * family entirely -- confirmed by checking, not assumed), so this is a
 * genuinely new port rather than the bme280/fxos8700/FRAM "reuse an
 * upstream driver" pattern Phase 3 established. stc3100_demo.c is the
 * Zephyr I2C plumbing (bus access, periodic polling); this file is the
 * hardware-agnostic decode, same split ant_fec.{h,c}/fec_demo.c already
 * established for FE-C.
 *
 * charge/current use stravaV10's own compute2Complement() (already
 * ported into this tree's lib/libraries/utils/utils.c during Phase 6) --
 * both registers are 14-bit signed values per the chip's own datasheet,
 * not a plain 16-bit two's complement, which is why a dedicated helper
 * exists instead of a plain cast. voltage/temperature/counter are plain
 * 16-bit unsigned magnitudes, ported as stravaV10 has them.
 *
 * Battery percentage is stravaV10's own percentageBatt() (also already
 * in utils.c) -- a piecewise-polynomial LiPo discharge-curve fit, with
 * BATT_INT_RES internal-resistance compensation applied to the raw
 * voltage under load. stc3100_percent() is a thin wrapper kept here so
 * callers don't need to know that history.
 */

#ifndef STC3100_H_
#define STC3100_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STC3100_I2C_ADDR 0x70

#define STC3100_REG_MODE 0
#define STC3100_REG_CONTROL 1
#define STC3100_REG_CHARGE_LOW 2
#define STC3100_REG_CHARGE_HIGH 3
#define STC3100_REG_COUNTER_LOW 4
#define STC3100_REG_COUNTER_HIGH 5
#define STC3100_REG_CURRENT_LOW 6
#define STC3100_REG_CURRENT_HIGH 7
#define STC3100_REG_VOLTAGE_LOW 8
#define STC3100_REG_VOLTAGE_HIGH 9
#define STC3100_REG_TEMPERATURE_LOW 10
#define STC3100_REG_TEMPERATURE_HIGH 11
#define STC3100_REG_DEVICE_ID 24

#define STC3100_MODE_RUN 0x10
#define STC3100_MODE_HIGHRES 0x02 /* STC3100_MODE_HIGHRES in stravaV10's stc3100_res_t */
#define STC3100_CONTROL_RESET 0x02
#define STC3100_CONTROL_IO_OD 0x01

/* One 10-byte block read starting at STC3100_REG_CHARGE_LOW -- the same
 * span stravaV10's own STC_READ_ALL_REGS/readChip() reads in one
 * transaction. */
struct stc3100_raw {
	uint8_t charge_low;
	uint8_t charge_high;
	uint8_t counter_low;
	uint8_t counter_high;
	uint8_t current_low;
	uint8_t current_high;
	uint8_t voltage_low;
	uint8_t voltage_high;
	uint8_t temperature_low;
	uint8_t temperature_high;
};

struct stc3100_reading {
	float voltage_v;
	float current_ma;
	float charge_mah;
	float temperature_c;
	float counter;
};

/* r_sens_mohm is the sense-resistor value in milliohms (100 in
 * stravaV10's STC3100_CUR_SENS_RES_MO). */
void stc3100_decode(const struct stc3100_raw *raw, int32_t r_sens_mohm,
		    struct stc3100_reading *out);

float stc3100_percent(float voltage_v, float current_ma);

#ifdef __cplusplus
}
#endif

#endif /* STC3100_H_ */
