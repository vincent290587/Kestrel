/*
 * GFX port Phase D: definitions for the globals vue_global.h/menu_host.h
 * declare -- see those headers' own comments for why `vue` (VueBase) and
 * `menu` (Menuable) are separate globals from gfx_demo.cpp's own private
 * `gfx` object.
 *
 * Deliberately NOT yet wired into main()'s boot sequence or pushed to the
 * real display -- gfx_demo.cpp's own gfx object already owns the one
 * physical LS027 buffer that's actively driven by sensor_screen_demo.c's
 * 1Hz redraw loop; making the menu actually appear on real glass needs
 * real display-arbitration design (pause that redraw while the menu is
 * open, push through the same display_write() pipeline) plus real GPIO
 * button wiring -- neither exists yet. This just defines the globals so
 * the menu system compiles and its real callback wiring (menu_content.cpp)
 * can be exercised once that integration work happens, likely as part of
 * porting Vue itself (see the GFX port plan in todo.md).
 */

#include "vue_global.h"
#include "menu_host.h"
#include "att_global.h"
#include "PowerZone.h"
#include "RRZone.h"
#include "SufferScore.h"
#include "g_structs.h"
#include "SegmentManager.h"

VueBase vue;
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
