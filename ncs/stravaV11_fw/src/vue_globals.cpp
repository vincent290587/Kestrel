/*
 * GFX port Phase D: definitions for the globals vue_global.h/menu_host.h
 * declare -- see those headers' own comments for why `vue` (now the real,
 * fully-assembled `Vue` class) and `menu` (a separate `Menuable` instance)
 * are distinct globals from gfx_demo.cpp's own private `gfx` object.
 *
 * Deliberately still NOT wired into main()'s boot sequence or pushed to
 * the real display -- gfx_demo.cpp's own gfx object still owns the one
 * physical LS027 buffer that's actively driven by sensor_screen_demo.c's
 * 1Hz redraw loop, and `vue.init()` is never called here (see Vue.cpp's
 * own top-of-file note plus main.cpp's Vue test block on stravaV11_app
 * for the real reason: Menuable::initMenu() rebinds file-scope static
 * page objects to whichever Menuable instance calls it, and `menu`
 * already has -- calling it a second time on `vue` would corrupt that).
 * Real display-arbitration (deciding which of gfx_demo.c's redraw loop vs.
 * vue.refresh() actually owns the physical screen) and GPIO button wiring
 * (driving vue.tasks() from real hardware input) are both still explicitly
 * out of scope for this update, by direction -- this only defines the
 * real Vue/Menuable globals so they exist and compile on real hardware,
 * same "infrastructure before consumption" precedent as several earlier
 * phases in this port (e.g. GPS_R/GPS_S wired into devicetree well before
 * anything drove a real reset pulse).
 */

#include "vue_global.h"
#include "menu_host.h"
#include "att_global.h"
#include "PowerZone.h"
#include "RRZone.h"
#include "SufferScore.h"
#include "g_structs.h"
#include "SegmentManager.h"
#include "Points.h"

Vue vue;
MenuHost menu;

/* GFX port Phase D: VuePRC.cpp's global -- the routes/ tier (Points,
 * Vecteur, ListePoints, Segment, Parcours) and SegmentManager are new to
 * this app as of VuePRC (every earlier screen here routed around them via
 * debug_screen_data.h's segment *count*, not the real types -- see that
 * header's own comment), copied from stravaV11_app unmodified, same
 * "already proven portable" precedent as everything else in lib/source/.
 * Starts empty (getNbSegs()==0), same "no ride data yet" honesty as
 * zPower/rrZones/suffer_score above. */
SegmentManager segMngr;

/* GFX port Phase D: see att_global.h's own comment -- stays zero-
 * initialized until Attitude (not ported) exists to populate it. */
SAtt att;

/* GFX port Phase D: VueFEC.cpp's globals. Same PowerZone/RRZone/
 * SufferScore classes as stravaV11_app (Phase 1), ported here too since
 * VueFEC is the first stravaV11_fw consumer. hrm_info/bsc_info/fec_info
 * (g_structs.c, also newly ported here) stay at their zero-initialized
 * defaults -- hrm_demo.c/bsc_demo.c/fec_demo.c each keep their own
 * separate internal state (m_bpm/m_speed/m_power_w etc.) rather than
 * populating these classic globals, same as before this update; wiring
 * them together is real follow-up work, not done here. Honest "no ride
 * data yet" defaults, not fake stand-ins. */
PowerZone zPower;
RRZone rrZones;
SufferScore suffer_score;
sPowerVector powerVector;

/* GFX port Phase D (Vue assembly): normally defined in Model.cpp
 * (stravaV11_app's own globals.cpp already carries the equivalent, per
 * Phase 1) -- Points.h's static object-count bookkeeping. Never actually
 * needed a definition here before now: Points.cpp was linked in as of the
 * VuePRC increment, but nothing reachable from vue_globals.cpp's own
 * globals ever actually *called* Point::Point()/~Point() until `Vue vue;`
 * above started actually pulling VueCRS/VuePRC's real code paths in
 * (previously dead-code-eliminated -- see the VueCRS increment's own
 * todo.md note on that). Caught at link time (`undefined reference to
 * Point::objectCount`), not by inspection. */
int Point2D::objectCount2D = 0;
int Point::objectCount = 0;
