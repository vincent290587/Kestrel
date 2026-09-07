#include "ant_fec.h"

#include <zephyr/sys/byteorder.h>

void ant_fec_page16_decode(const uint8_t *payload, struct ant_fec_page16 *out)
{
	out->equipment_type = payload[0];
	out->elapsed_time = payload[1];
	out->distance_traveled = payload[2];
	out->speed_mm_s = sys_get_le16(&payload[3]);
	out->heart_rate = payload[5];
	out->cap_state = payload[6];
}

void ant_fec_page25_decode(const uint8_t *payload, struct ant_fec_page25 *out)
{
	out->event_count = payload[0];
	out->inst_cad = payload[1];
	out->acc_power = sys_get_le16(&payload[2]);
	out->inst_power = payload[4] | ((uint16_t)(payload[5] & 0x0f) << 8);
	out->status = (payload[5] & 0xf0) >> 4;
	out->flags = payload[6];
}

void ant_fec_page49_encode(uint8_t *payload, uint16_t target_power_w)
{
	payload[0] = 0xff;
	payload[1] = 0xff;
	payload[2] = 0xff;
	payload[3] = 0xff;
	payload[4] = 0xff;
	/* LSB 0.25 W (ANT_FEC_PAGE49_TARGET_POWER_LSB in stravaV10). */
	sys_put_le16((uint16_t)(target_power_w * 4u), &payload[5]);
}

void ant_fec_page51_encode(uint8_t *payload, float grade_pct, float rolling_resistance)
{
	payload[0] = 0xff;
	payload[1] = 0xff;
	payload[2] = 0xff;
	payload[3] = 0xff;

	/* LSB 0.01%, +200% offset (ANT_FEC_PAGE51_SLOPE_LSB in stravaV10). */
	uint16_t raw_grade = (uint16_t)((grade_pct + 200.0f) * 100.0f);

	sys_put_le16(raw_grade, &payload[4]);

	/* LSB per stravaV10's ANT_FEC_PAGE51_ROLL_RES_LSB -- see ant_fec.h's
	 * top-of-file note on this constant's unverified accuracy. Clamp
	 * rather than let a wraparound silently send a wildly wrong value.
	 */
	float raw_roll_res = rolling_resistance * 100000.0f;

	if (raw_roll_res < 0.0f) {
		raw_roll_res = 0.0f;
	} else if (raw_roll_res > 255.0f) {
		raw_roll_res = 255.0f;
	}
	payload[6] = (uint8_t)raw_roll_res;
}
