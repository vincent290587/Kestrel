/*
 * Global objects that stravaV10's Model.cpp normally defines and that this
 * logic-only slice still needs a definition for (PowerZone reads FTP from
 * u_settings). Model.cpp itself isn't ported yet -- see CLAUDE.md.
 */

#include "UserSettings.h"
#include "Points.h"
#include "ZephyrGFX.h"
#include "vue_global.h"
#include "menu_host.h"
#include "att_global.h"
#include "Locator.h"
#include "Segment.h"
#include "PowerZone.h"
#include "RRZone.h"
#include "SufferScore.h"
#include "g_structs.h"
#include "SegmentManager.h"

UserSettings u_settings;

/* GFX port Phase D: the global `vue` MenuObjects.cpp/Menuable.cpp/
 * VueDebug.cpp/VueGPS.cpp/VueFEC.cpp draw into -- see vue_global.h's own
 * comment for what VueBase is and why. */
VueBase vue;

/* GFX port Phase D: the global Menuable instance menu_content.cpp builds
 * the menu tree against -- see menu_host.h's own comment for why this is
 * separate from `vue`. */
MenuHost menu;

/* GFX port Phase D: see att_global.h's own comment -- stays zero-
 * initialized until Attitude (not ported) exists to populate it. */
SAtt att;

/* GFX port Phase D: VueDebug.cpp's global `locator` (via
 * Locator::displayGPS2()) and `mes_segments` (real ListeSegments, just
 * empty until real route/segment loading is wired into this app --
 * neither app does that yet). Distinct from main.cpp's own local
 * `Locator locator;` in its GPS smoke test, which stays local since
 * nothing else needs to share it. */
Locator locator;
ListeSegments mes_segments;

/* GFX port Phase D: VuePRC.cpp's global -- the same SegmentManager class
 * ported in Phase 1 (segment scoring/ordering bookkeeping), now with a
 * real, live instance. Starts empty (getNbSegs()==0), same "no ride data
 * yet" honesty as zPower/rrZones/suffer_score above -- nothing loads real
 * segments into it yet. */
SegmentManager segMngr;

/* GFX port Phase D: VueFEC.cpp's globals -- zPower/rrZones are the same
 * PowerZone/RRZone classes already ported (Phase 1) and already
 * exercised by main.cpp's own smoke test (as local objects there); these
 * are the real, shared instances a screen would read from. suffer_score/
 * powerVector likewise. All start at their default-constructed/
 * zero-initialized state -- honest "no ride data yet" until Boucle*
 * (not ported) or something else feeds them. */
PowerZone zPower;
RRZone rrZones;
SufferScore suffer_score;
sPowerVector powerVector;

// Also normally defined in Model.cpp, even though they belong to Points.h's
// object-count bookkeeping.
int Point2D::objectCount2D = 0;
int Point::objectCount = 0;
