/*
 * Global objects that stravaV10's Model.cpp normally defines and that this
 * logic-only slice still needs a definition for (PowerZone reads FTP from
 * u_settings). Model.cpp itself isn't ported yet -- see CLAUDE.md.
 */

#include "UserSettings.h"
#include "Points.h"

UserSettings u_settings;

// Also normally defined in Model.cpp, even though they belong to Points.h's
// object-count bookkeeping.
int Point2D::objectCount2D = 0;
int Point::objectCount = 0;
