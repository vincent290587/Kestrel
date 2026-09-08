#ifndef STC3100_DEMO_H_
#define STC3100_DEMO_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Starts the periodic STC3100 fuel-gauge poll (1Hz). Safe to call even
 * on boards with no STC3100 on the bus (the DK) -- each read cycle just
 * fails cleanly and getters keep reporting "no reading yet". Must run
 * after stc3100_power_latch_hold() (main.c) has already set MODE_RUN --
 * this only refines the mode to include the resolution bits stravaV10's
 * real init() used (STC3100_MODE_HIGHRES), never clearing MODE_RUN, so
 * the power latch stays held throughout. */
void stc3100_demo_start(void);

/* All four return false (value left untouched) until the first
 * successful read -- same "no stale-looking data before it's real"
 * convention hrm_demo_is_paired()/gps_demo_get_altitude() already use. */
bool stc3100_demo_get_voltage_v(float *voltage_v);
bool stc3100_demo_get_current_ma(float *current_ma);
bool stc3100_demo_get_charge_mah(float *charge_mah);
bool stc3100_demo_get_percent(float *percent);

/* Milliseconds since the last successful I2C read, or UINT32_MAX if none
 * ever succeeded. This chip is a fixed, onboard, always-on-the-bus part
 * (not a wireless sensor that can go out of range), so real staleness is
 * a much rarer scenario than for hrm_demo/bsc_demo/fec_demo -- added for
 * consistency with those, not because a disconnect scenario is expected
 * here. */
uint32_t stc3100_demo_get_age_ms(void);

/* Logs the current reading (or "no reading yet") -- on-demand
 * introspection via cmd_console.c's "BATT" command, same purpose as
 * "DM LIST" for the device manager. */
void stc3100_demo_log_reading(void);

#ifdef __cplusplus
}
#endif

#endif /* STC3100_DEMO_H_ */
