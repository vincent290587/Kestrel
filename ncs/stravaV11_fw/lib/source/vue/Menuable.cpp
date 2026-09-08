/*
 * Menuable.cpp
 *
 * GFX port Phase D: ported the reusable class mechanics only --
 * constructor, closeMenu()/goToParentPage()/goToChildPage()/
 * propagateEvent()/tasksMenu() -- unmodified from stravaV10's own logic.
 * stravaV10's initMenu() is NOT here: it hardcodes this application's
 * actual menu tree (mode-switching tied to Boucle*, which isn't ported;
 * ANT+ pairing/settings tied to ant_device_manager/UserSettings, which
 * this port has real equivalents for now). Since that content genuinely
 * differs between a native_sim test tree and the real hardware-wired
 * tree, Menuable::initMenu() is defined in a separate, environment-
 * specific file instead (menu_content.cpp) -- perfectly valid C++, a
 * class's member functions don't all need to live in one file, and it
 * keeps this file identical between stravaV11_app and stravaV11_fw.
 *
 * refreshMenu() also isn't ported as-is: stravaV10's version cancels a
 * pending display-refresh task-manager timer (m_tasks_id.ls027_id /
 * w_task_delay_cancel()) so menu navigation redraws immediately instead
 * of waiting for the next tick -- there's no equivalent "pending redraw"
 * abstraction in this port yet (every demo screen just redraws
 * unconditionally on its own timer). Left as a no-op for now, same
 * "adapter that no-ops until the real thing exists" precedent as Phase 1's
 * notifications_segNotify() stub.
 */

#include "Menuable.h"
#include "MenuObjects.h"
#include "segger_wrapper.h"
#include "assert_wrapper.h"
#include "millis.h"

Menuable::Menuable()
{
	m_is_menu_selected = false;
	p_cur_page = nullptr;
}

Menuable::~Menuable()
{
	// Auto-generated destructor stub
}

void Menuable::closeMenu()
{
	m_is_menu_selected = false;
	p_cur_page = p_root_page;
}

void Menuable::goToParentPage(void)
{
	if (p_cur_page->getParent()) {
		p_cur_page = p_cur_page->getParent();
	} else {
		// page has no parent
		this->closeMenu();
	}
}

void Menuable::goToChildPage(MenuPage *page)
{
	ASSERT(page);
	p_cur_page = page;
}

void Menuable::refreshMenu(void)
{
	// See top-of-file note: no pending-redraw abstraction to cancel yet.
}

void Menuable::propagateEvent(eButtonsEvent event)
{
	/* Boot-time debounce: ignore button events in the first 5s, same as
	 * stravaV10's original -- avoids a spurious power-on button noise
	 * immediately opening the menu. */
	if (millis() < 5000) {
		return;
	}

	LOG_INFO("Menu event %u", event);

	if (!m_is_menu_selected && eButtonsEventCenter == event) {
		m_is_menu_selected = true;
	} else if (m_is_menu_selected) {
		p_cur_page->propagateEvent(event);
	}

	if (!m_is_menu_selected) {
		return;
	}

	// trigger display task
	this->refreshMenu();
}

void Menuable::tasksMenu(void)
{
	p_cur_page->render();
}
