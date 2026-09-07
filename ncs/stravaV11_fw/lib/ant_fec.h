/*
 * ANT+ FE-C (Fitness Equipment Control) wire-format layer, ported from
 * stravaV10's libraries/ant_profiles/ant_fec -- sdk-ant ships no FE-C
 * profile library (unlike HRM/BSC, which reused ready-made ant_hrm/
 * ant_bsc), so unlike those two ports this one carries stravaV10's own
 * page-parsing logic over, same "keep the wire-format logic, rebuild the
 * plumbing" approach bt_cp_client.c used against Zephyr's Bluetooth
 * host -- fec_demo.c is the plumbing, this file is the wire format.
 *
 * Only the pages stravaV10's rf/fec.c actually consumed are ported:
 * page 16 (general FE data -- elapsed time) and page 25 (trainer-specific
 * data -- instantaneous power/cadence) for RX, and pages 49 (target
 * power) / 51 (track resistance) for TX control -- stravaV10's own
 * sFecControl only ever drives those two control types (see
 * mk64f_parser.h's eFecControlType: TargetPower or Slope, no basic-
 * resistance option), so page 48 (basic resistance) was never actually
 * sent and is not ported. Pages 1/2/17/21/54/80/81 were only ever logged
 * in stravaV10, never consumed -- also not ported, same "port what's
 * actually used" precedent as Locator.cpp dropping dead code in Phase 6.
 *
 * Byte layouts cross-checked field-by-field against stravaV10's own
 * ant_fec_page_16.c/ant_fec_page_25.c/ant_fec_page_49.c/ant_fec_page_51.c
 * (struct-overlay style there; sys_put_le16/sys_get_le16 here, matching
 * this port's established wire-format convention). The page 51 rolling-
 * resistance LSB (ANT_FEC_PAGE51_ROLL_RES_LSB, 1/100000 in stravaV10) is
 * carried over unchanged even though it doesn't match this author's best
 * recollection of the public ANT+ FE-C spec value (5e-5, i.e. 1/20000) --
 * kept as stravaV10 had it rather than "fixed" from memory alone, since
 * unlike the BSC speed-coefficient bug (Phase 5), there's no sdk-ant
 * reference sample to cross-check FE-C values against. Flagged here for
 * whoever eventually tests real resistance control against a trainer.
 */

#ifndef ANT_FEC_H_
#define ANT_FEC_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* General FE data (page 16). Payload is the 7 bytes after the page-number
 * byte: equipment_type, elapsed_time (LSB 1/4 s, 8-bit, wraps every 64s),
 * distance_traveled, speed (LE16, LSB 0.001 m/s), heart_rate, cap_state. */
struct ant_fec_page16 {
	uint8_t equipment_type;
	uint8_t elapsed_time;
	uint8_t distance_traveled;
	uint16_t speed_mm_s;
	uint8_t heart_rate;
	uint8_t cap_state;
};

/* Trainer/torque data (page 25). event_count/inst_cad are 8-bit counters;
 * acc_power is LE16 watts (accumulated, wraps); inst_power is a 12-bit
 * watts value packed across inst_power_lsb (byte 4) and the low nibble of
 * byte 5, with trainer status in that byte's high nibble. */
struct ant_fec_page25 {
	uint8_t event_count;
	uint8_t inst_cad;
	uint16_t acc_power;
	uint16_t inst_power;
	uint8_t status; /* 4 bits */
	uint8_t flags;
};

void ant_fec_page16_decode(const uint8_t *payload, struct ant_fec_page16 *out);
void ant_fec_page25_decode(const uint8_t *payload, struct ant_fec_page25 *out);

/* Target Power (page 49): commands the trainer to hold a fixed power, LSB
 * 0.25 W. `payload` must be 7 bytes (the page-payload region, page-number
 * byte handled by the caller). */
void ant_fec_page49_encode(uint8_t *payload, uint16_t target_power_w);

/* Track Resistance (page 51): commands the trainer to simulate a grade,
 * LSB 0.01% with a +200% (20000 raw) offset per stravaV10's own
 * ANT_FEC_PAGE51_SLOPE_LSB, plus a rolling-resistance coefficient byte
 * (see this header's top-of-file note on that constant). */
void ant_fec_page51_encode(uint8_t *payload, float grade_pct, float rolling_resistance);

#ifdef __cplusplus
}
#endif

#endif /* ANT_FEC_H_ */
