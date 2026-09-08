/*
 * GFX port Phase D: Menuable::initMenu()'s definition for stravaV11_fw --
 * see Menuable.cpp's own top-of-file note for why this lives in its own
 * file, and stravaV11_app's copy of this file for the native_sim version
 * this was validated against first (same tree, stub pairing callbacks
 * there since ant_dm_demo.c is real-ANT+-hardware-only code that doesn't
 * exist on native_sim).
 *
 * Real wiring here, not stubs: "Pair HRM/BSC/FEC" call this port's actual
 * ant_dm_demo_search_start() (Phase 5), "Set FTP"/"Set Weight" call
 * settings_demo_set_ftp()/_set_weight() (just-ported UserSettings/FRAM
 * persistence). This is the "concrete payoff" the GFX port plan called
 * out: real on-device pairing/settings UI, not just RTT/USB text
 * commands -- though see this file's own note on ant_dm_demo_search_list()
 * for what's still not wired up.
 *
 * NOT done: sd_format_start() ("! Format !" in stravaV10's original) --
 * sd_format.h has no extern "C" guard, so #including it from this .cpp
 * would try to link against sd_format.c's C-linkage symbol under C++
 * name mangling and fail; fixing that header wasn't in scope for this
 * change. Also not done: fxos_calibration_start() (magnetometer
 * calibration -- Attitude.cpp/fxos calibration logic isn't ported) and
 * every ride-mode-switching item (needs Boucle*, the plan's whole reason
 * for decoupling this UI port in the first place).
 */

#include "Menuable.h"
#include "MenuObjects.h"
#include "UserSettings.h"
#include "menu_host.h"
#include "ant_dm_demo.h"
#include "settings_demo.h"
#include "segger_wrapper.h"

static MenuPageItems m_root_page(menu, nullptr);
static MenuPageSetting page_value(menu, &m_root_page);

static eFuncMenuAction _pair_hrm(int var)
{
	ant_dm_demo_search_start(ANT_DM_SENSOR_HRM);
	return eFuncMenuActionEndMenu;
}

static eFuncMenuAction _pair_bsc(int var)
{
	ant_dm_demo_search_start(ANT_DM_SENSOR_BSC);
	return eFuncMenuActionEndMenu;
}

static eFuncMenuAction _pair_fec(int var)
{
	ant_dm_demo_search_start(ANT_DM_SENSOR_FEC);
	return eFuncMenuActionEndMenu;
}

/* user_settings_get() (declared in UserSettings.h) is the same raw-struct
 * "get" side settings_demo.cpp itself uses internally (see its own
 * top-of-file note on why -- keeps UserSettings a fully unmodified port,
 * no setter added to the class itself). settings_demo_set_ftp()/
 * _set_weight() are the real "set + persist to FRAM" side. */
static int get_cur_ftp(int var)
{
	return user_settings_get()->FTP;
}

static int set_cur_ftp(int var)
{
	settings_demo_set_ftp((uint16_t)var);
	return 0;
}

static eFuncMenuAction _set_ftp(int var)
{
	sSettingsCallbacks callbacks = {get_cur_ftp, set_cur_ftp};

	page_value.setCallbacks(callbacks);
	page_value.setName("FTP:");

	return eFuncMenuActionNone;
}

static int get_cur_weight(int var)
{
	return user_settings_get()->weight;
}

static int set_cur_weight(int var)
{
	settings_demo_set_weight((uint16_t)var);
	return 0;
}

static eFuncMenuAction _set_weight(int var)
{
	sSettingsCallbacks callbacks = {get_cur_weight, set_cur_weight};

	page_value.setCallbacks(callbacks);
	page_value.setName("Weight:");

	return eFuncMenuActionNone;
}

void Menuable::initMenu(void)
{
	MenuItem item_prm(m_root_page, "Pair HRM", _pair_hrm);
	MenuItem item_psc(m_root_page, "Pair BSC", _pair_bsc);
	MenuItem item_pec(m_root_page, "Pair FEC", _pair_fec);
	MenuItem item_ftp(m_root_page, "Set FTP", _set_ftp, &page_value);
	MenuItem item_wei(m_root_page, "Set Weight", _set_weight, &page_value);

	m_root_page.addItem(item_prm);
	m_root_page.addItem(item_psc);
	m_root_page.addItem(item_pec);
	m_root_page.addItem(item_ftp);
	m_root_page.addItem(item_wei);

	p_root_page = &m_root_page;

	this->closeMenu();
}
