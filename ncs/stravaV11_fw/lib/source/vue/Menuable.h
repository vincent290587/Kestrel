/*
 * Menuable.h
 *
 *  Created on: 13 d�c. 2017
 *      Author: Vincent
 */

#ifndef SOURCE_VUE_MENUABLE_H_
#define SOURCE_VUE_MENUABLE_H_

#include <stdint.h>
#include <stdbool.h>
#include <button.h>
#include <list>
#include "WString.h"

typedef enum {
	eMenuableModeAffi,
	eMenuableModeMenu,
	eMenuableModeSubMenu,
} eMenuableMode;

class MenuPage;

class Menuable {
public:
	Menuable();
	virtual ~Menuable();

	void initMenu(void);

	void refreshMenu(void);

	void propagateEvent(eButtonsEvent event);

	void tasksMenu(void);

	void goToParentPage(void);

	void goToChildPage(MenuPage *page);

	void closeMenu();

	virtual void refresh(void)=0;

	bool m_is_menu_selected;

	std::list<MenuPage> m_menus;
protected:

	MenuPage *p_cur_page;

	/* GFX port Phase D addition, not in stravaV10's original: the page
	 * closeMenu() returns to. stravaV10 hardcoded a reference to a
	 * file-scope static (m_main_page) defined alongside initMenu() in the
	 * same .cpp -- moved here as a real member instead, since this port's
	 * initMenu() is defined in a separate, environment-specific file (see
	 * Menuable.cpp's own top-of-file note) and closeMenu() still needs to
	 * know the root page regardless of which file set it up. Set once by
	 * initMenu(). */
	MenuPage *p_root_page = nullptr;
};

#endif /* SOURCE_VUE_MENUABLE_H_ */
