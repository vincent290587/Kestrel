/*
 * GFX port Phase D: Menuable::initMenu()'s definition for stravaV11_app
 * (native_sim) -- see Menuable.cpp's own top-of-file note for why this
 * lives in its own file rather than in Menuable.cpp itself.
 *
 * Deliberately NOT stravaV10's original menu tree: that one's root page
 * has mode-switching items (Mode FEC/CRS/PRC/Zwift/DBG) tied to
 * boucle__change_mode()/Boucle* state machines, which aren't ported (the
 * single largest remaining item in the whole project -- see the GFX port
 * plan in todo.md, which deliberately decouples this UI port from that).
 * This tree covers only what's genuinely real and testable right now:
 * the settings/pairing subtree, using this port's own real UserSettings
 * (already ported, Phase 1) directly -- not stubbed. ANT+ pairing itself
 * is stubbed here (ant_dm_demo.c is stravaV11_fw-only, real ANT+ hardware
 * code that doesn't exist on native_sim); the real wiring lives in
 * stravaV11_fw's own copy of this file.
 *
 * Also a real, deliberate scope cut from stravaV10's original pairing
 * flow: stravaV10's "Pair HRM" opened a MenuPagePairing showing live
 * discovered candidates as selectable items (via
 * ant_device_manager_get_sensors_list()). ant_dm_demo.h/.c (this port's
 * real ANT+ device manager) doesn't yet expose an equivalent accessor for
 * its candidate list -- only a log-only ant_dm_demo_search_list(). So for
 * now, the menu's pairing items only trigger a search; picking a
 * discovered candidate is still done via cmd_console.c's "DM LIST"/
 * "DM PICK <n>" commands, not from the menu. A real, flagged gap, not
 * silently skipped -- exposing that candidate list is natural follow-up
 * work once this lands.
 */

#include "Menuable.h"
#include "MenuObjects.h"
#include "UserSettings.h"
#include "menu_host.h"
#include "segger_wrapper.h"

extern UserSettings u_settings;

static MenuPageItems m_root_page(menu, nullptr);
static MenuPageSetting page_value(menu, &m_root_page);

/* Test-observable: how many times each pairing stub was invoked, and with
 * what would-be sensor type -- lets the native_sim smoke test verify menu
 * dispatch reaches the right callback, without needing the real ANT+
 * stack this file can't have on native_sim. */
int g_menu_test_pair_hrm_calls;
int g_menu_test_pair_bsc_calls;
int g_menu_test_pair_fec_calls;

static eFuncMenuAction _pair_hrm(int var)
{
	g_menu_test_pair_hrm_calls++;
	LOG_INFO("menu: would call ant_dm_demo_search_start(HRM)");
	return eFuncMenuActionEndMenu;
}

static eFuncMenuAction _pair_bsc(int var)
{
	g_menu_test_pair_bsc_calls++;
	LOG_INFO("menu: would call ant_dm_demo_search_start(BSC)");
	return eFuncMenuActionEndMenu;
}

static eFuncMenuAction _pair_fec(int var)
{
	g_menu_test_pair_fec_calls++;
	LOG_INFO("menu: would call ant_dm_demo_search_start(FEC)");
	return eFuncMenuActionEndMenu;
}

static int get_cur_ftp(int var)
{
	return u_settings.getFTP();
}

static int set_cur_ftp(int var)
{
	sUserParameters *settings = user_settings_get();

	settings->FTP = (uint16_t)var;
	u_settings.writeConfig();

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
	return u_settings.getWeight();
}

static int set_cur_weight(int var)
{
	sUserParameters *settings = user_settings_get();

	settings->weight = (uint16_t)var;
	u_settings.writeConfig();

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
