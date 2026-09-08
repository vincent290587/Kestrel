/*
 * Wires Phase 1's UserSettings (until now only ever exercised on
 * native_sim in stravaV11_app, backed by a stub that always fails) onto
 * the real FRAM chip in stravaV11_fw -- same "copy the file unmodified,
 * write a thin C-linkage wrapper + a real adapter" split as Locator/
 * gps_demo.cpp used for GPS in Phase 11. UserSettings.{h,cpp} themselves
 * are byte-identical to stravaV11_app's copy; only adapters/fram_zephyr.c
 * (real eeprom_read()/eeprom_write() against the "fram" devicetree node,
 * replacing stravaV11_app's adapters/fram_stub.c which always returns
 * false) is new.
 *
 * user_settings_get() (declared in UserSettings.h, defined in
 * UserSettings.cpp) returns a pointer to the single static
 * sUserParameters instance every UserSettings object's m_params
 * reference aliases -- used here instead of adding a setter to the
 * ported UserSettings class, so that class stays a fully unmodified
 * port like every other ported file in this project.
 */

#include "settings_demo.h"

#include <zephyr/sys/printk.h>

#include "UserSettings.h"

static UserSettings s_settings;

void settings_demo_dump(void)
{
	sUserParameters *p = user_settings_get();

	printk("settings: FTP=%u weight=%u hrm=%u bsc=%u fec=%u gla=%u\n", p->FTP, p->weight,
	       p->hrm_devid, p->bsc_devid, p->fec_devid, p->gla_devid);
}

void settings_demo_init(void)
{
	s_settings.enforceConfigVersion();
	settings_demo_dump();
}

void settings_demo_reset(void)
{
	s_settings.resetConfig();
	settings_demo_dump();
}

static void set_field(uint16_t *field, uint16_t value, const char *name)
{
	*field = value;

	bool ok = s_settings.writeConfig();

	printk("settings: %s set to %u, writeConfig() -> %d\n", name, value, (int)ok);
}

void settings_demo_set_ftp(uint16_t ftp)
{
	set_field(&user_settings_get()->FTP, ftp, "FTP");
}

void settings_demo_set_weight(uint16_t weight)
{
	set_field(&user_settings_get()->weight, weight, "weight");
}

void settings_demo_set_hrm(uint16_t dev_id)
{
	set_field(&user_settings_get()->hrm_devid, dev_id, "hrm");
}

void settings_demo_set_bsc(uint16_t dev_id)
{
	set_field(&user_settings_get()->bsc_devid, dev_id, "bsc");
}

void settings_demo_set_fec(uint16_t dev_id)
{
	set_field(&user_settings_get()->fec_devid, dev_id, "fec");
}

void settings_demo_set_gla(uint16_t dev_id)
{
	set_field(&user_settings_get()->gla_devid, dev_id, "gla");
}
