#ifndef ANT_DM_DEMO_H_
#define ANT_DM_DEMO_H_

#ifdef __cplusplus
extern "C" {
#endif

enum ant_dm_sensor_type {
	ANT_DM_SENSOR_NONE,
	ANT_DM_SENSOR_HRM,
	ANT_DM_SENSOR_BSC,
	ANT_DM_SENSOR_FEC,
};

int ant_dm_demo_start(void);

/* Starts a background scan for the given sensor type -- resets the
 * candidate list and opens the shared background-scan channel filtered
 * to that type's ANT+ device type. */
void ant_dm_demo_search_start(enum ant_dm_sensor_type type);

/* Logs the candidates seen so far (device ID + RSSI), indexed the same
 * way ant_dm_demo_search_validate() expects. */
void ant_dm_demo_search_list(void);

/* Commits candidate list index `idx` as the real device for the
 * in-progress search's sensor type: closes the background-scan channel
 * and reprograms the corresponding profile channel (HRM/BSC/FEC) to that
 * device number. That profile channel's own reconnect-on-close/pairing
 * logic (already in hrm_demo.c/bsc_demo.c/fec_demo.c) picks up the new
 * ID with no further wiring needed. */
void ant_dm_demo_search_validate(int idx);

/* Cancels an in-progress search: closes the background-scan channel,
 * leaves the profile channel's current device number untouched. */
void ant_dm_demo_search_cancel(void);

#ifdef __cplusplus
}
#endif

#endif /* ANT_DM_DEMO_H_ */
