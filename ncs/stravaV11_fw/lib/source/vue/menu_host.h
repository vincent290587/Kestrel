/*
 * GFX port Phase D: a minimal concrete Menuable, standing in for what the
 * eventual ported Vue class will provide. Menuable::refresh() is pure
 * virtual (stravaV10's Vue implements it as part of its own refresh()
 * cycle), so some concrete subclass is needed to instantiate a Menuable
 * at all -- this is deliberately the smallest one that compiles, not a
 * guess at what Vue's real refresh() should do.
 *
 * Kept separate from the global `vue` (ZephyrGFX, see vue_global.h):
 * MenuPage/MenuPageItems take a Menuable& purely for navigation
 * callbacks (goToParentPage()/goToChildPage()/closeMenu()) -- in
 * stravaV10's original, Vue conveniently satisfied both roles at once
 * (it multiply-inherits Menuable alongside Adafruit_GFX), but this port
 * doesn't have that combined class yet. Actual rendering
 * (MenuItem::render() etc. in MenuObjects.cpp) still goes through the
 * global `vue` directly, unchanged from stravaV10's own code -- these two
 * globals serve genuinely separate roles until Vue itself is ported.
 */

#ifndef SOURCE_VUE_MENU_HOST_H_
#define SOURCE_VUE_MENU_HOST_H_

#include "Menuable.h"

class MenuHost : public Menuable {
public:
	void refresh(void) override
	{
	}
};

extern MenuHost menu;

#endif /* SOURCE_VUE_MENU_HOST_H_ */
