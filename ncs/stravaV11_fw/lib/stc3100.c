#include "stc3100.h"
#include "utils.h"

void stc3100_decode(const struct stc3100_raw *raw, int32_t r_sens_mohm,
		    struct stc3100_reading *out)
{
	uint16_t voltage_raw = ((uint16_t)raw->voltage_high << 8) | raw->voltage_low;

	/* LSB is 2.44mV (stravaV10's computeVoltage()); registers here are
	 * called "V" but the chip and stravaV10 both treat the value as
	 * volts once scaled. */
	out->voltage_v = (float)voltage_raw * 0.00244f;

	uint16_t counter_raw = ((uint16_t)raw->counter_high << 8) | raw->counter_low;

	out->counter = (float)counter_raw;

	uint16_t temp_raw = ((uint16_t)raw->temperature_high << 8) | raw->temperature_low;

	out->temperature_c = (float)temp_raw * 0.125f;

	float charge_raw = compute2Complement(raw->charge_high, raw->charge_low);

	/* LSB = 6.7 uV.h, charge in mA.h (stravaV10's computeCharge()). */
	out->charge_mah = charge_raw * 6.7f / (float)r_sens_mohm;

	float current_raw = compute2Complement(raw->current_high, raw->current_low);

	/* LSB = 11.77uV (stravaV10's computeCurrent()). */
	out->current_ma = current_raw * 11.77f / (float)r_sens_mohm;
}

float stc3100_percent(float voltage_v, float current_ma)
{
	return percentageBatt(voltage_v, current_ma);
}
