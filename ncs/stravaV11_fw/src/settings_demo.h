#ifndef SETTINGS_DEMO_H_
#define SETTINGS_DEMO_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reads FRAM via UserSettings::enforceConfigVersion() (CRC + version
 * check; factory-resets and writes real defaults to FRAM on first-ever
 * boot or a corrupt/blank chip), then logs the result. Call once, early.
 * Degrades cleanly (stays at in-memory defaults) if the FRAM device isn't
 * ready. Not dependent on fram_test.h's "FRAM TEST" command having run
 * first -- that's now a separate, on-demand diagnostic against a
 * different offset (see FRAM_DEMO_TEST_OFFSET in main.c), not a
 * prerequisite. */
void settings_demo_init(void);

/* Logs the currently in-memory settings (FTP/weight/device IDs )-- does
 * NOT re-read FRAM, same "report what's cached" convention as
 * stc3100_demo_log_reading() etc. */
void settings_demo_dump(void);

/* Explicit factory reset: rewrites in-memory defaults and persists them
 * to FRAM via UserSettings::resetConfig(). */
void settings_demo_reset(void);

/* Each of these sets one field in-memory and persists the whole settings
 * block to FRAM via UserSettings::writeConfig() -- one per cmd_console
 * "SETTINGS SET <FIELD> <n>" command. FTP/weight are stravaV10's
 * rider-profile fields (source/parameters.h's USER_FTP/USER_WEIGHT
 * defaults); HRM/BSC/FEC/GLA are the four ANT+ device-number fields
 * ant_device_manager.h's UserSettings::resetConfig() factory-defaults
 * (this is how a real ant_dm_demo.c pairing would eventually persist a
 * pick, once that wiring lands -- see that header's own comment). */
void settings_demo_set_ftp(uint16_t ftp);
void settings_demo_set_weight(uint16_t weight);
void settings_demo_set_hrm(uint16_t dev_id);
void settings_demo_set_bsc(uint16_t dev_id);
void settings_demo_set_fec(uint16_t dev_id);
void settings_demo_set_gla(uint16_t dev_id);

#ifdef __cplusplus
}
#endif

#endif /* SETTINGS_DEMO_H_ */
