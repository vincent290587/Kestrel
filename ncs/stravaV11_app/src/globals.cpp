/*
 * Global objects that stravaV10's Model.cpp normally defines and that this
 * logic-only slice still needs a definition for (PowerZone reads FTP from
 * u_settings). Model.cpp itself isn't ported yet -- see CLAUDE.md.
 */

#include "UserSettings.h"
#include "Points.h"
#include "ZephyrGFX.h"
#include "menu_host.h"

UserSettings u_settings;

/* GFX port Phase D: the global `vue` MenuObjects.cpp/Menuable.cpp draw
 * into -- see vue_global.h's own comment for why a plain ZephyrGFX
 * instance is sufficient here, not a full Vue-equivalent class yet. */
ZephyrGFX vue;

/* GFX port Phase D: the global Menuable instance menu_content.cpp builds
 * the menu tree against -- see menu_host.h's own comment for why this is
 * separate from `vue`. */
MenuHost menu;

// Also normally defined in Model.cpp, even though they belong to Points.h's
// object-count bookkeeping.
int Point2D::objectCount2D = 0;
int Point::objectCount = 0;
