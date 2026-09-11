/*
 * GFX port Phase D (display arbitration): resolves the "who actually owns
 * the physical LS027 buffer" question CLAUDE.md flagged as deferred since
 * the Vue-assembly increment. Found while wiring this up, not assumed:
 * sensor_screen_demo.c's gfx_demo_show_sensors() screen -- despite being
 * real, working, and confirmed on real glass in an earlier session (per
 * CLAUDE.md's own history) -- was never actually being *called* from
 * main()'s current boot sequence (sensor_screen_demo_start() has no call
 * site anywhere in src/, confirmed by grepping directly). So this isn't
 * really "replacing a competing loop" the way the original framing
 * implied; it's "wiring in the first one", since nothing has been
 * periodically redrawing the real display since whatever earlier session
 * last touched main()'s boot sequence.
 *
 * `vue` is set to eVueGlobalScreenDEBUG, not the default eVueGlobalScreenCRS,
 * and that's a deliberate, load-bearing choice, not an arbitrary one:
 * VueDebug::displayDebug() is the *only* one of Vue's four screen modes
 * whose data sources are actually wired to real, live globals right now
 * (locator, fed by real GPS UART parsing since Phase 6/11; debug_screen_data.h's
 * real STC3100 current/voltage getters, already used on this app since
 * Phase 3/11). VueCRS/VueFEC/VuePRC all read hrm_info/bsc_info/att/segMngr/
 * etc., which -- per this port's own VueFEC-increment note -- "stay at
 * their zero-initialized defaults... wiring them together is real
 * follow-up work, not done here." Defaulting the live screen to CRS mode
 * would have shown a screen full of real-looking but entirely fake zeros
 * (Dist/Pwr/Speed/Climb all reading 0) -- a regression in honesty
 * compared to gfx_demo_show_sensors()'s own "-- (search)"/stale-cross
 * convention, which correctly distinguished "no data yet" from a real
 * zero reading. DEBUG mode has no such gap: every field it draws already
 * reflects something real. Switching the live screen to CRS/FEC/PRC is
 * real, separate follow-up work (wiring hrm_info/bsc_info/att from
 * hrm_demo.c/bsc_demo.c/gps_demo.cpp's own existing internal state into
 * the classic globals those screens read), not done here.
 *
 * vue.init() is NOT called here, deliberately: it calls initMenu()
 * internally, which -- per Vue.cpp's own top-of-file note -- rebinds
 * file-scope static menu-page objects to whichever Menuable instance
 * calls it. GPIO buttons aren't wired this round (explicit direction),
 * so the menu can never actually open on real hardware yet regardless --
 * building its tree here would be pure, unnecessary risk for zero
 * present benefit. The three other things init() does (setRotation(3),
 * setTextWrap(false), setFont(&Org_01)) are called directly instead,
 * matching gfx_demo.cpp's own gfx_ready() lazy-init of its separate `gfx`
 * object at the same rotation/font.
 */

#include <zephyr/kernel.h>

#include "vue_global.h"
#include "Org_01.h"
#include "gfx_demo.h"

#include "vue_demo.h"

#define VUE_DEMO_REFRESH_MS 1000

static bool s_vue_ready;

static void vue_demo_work_handler(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(vue_demo_work, vue_demo_work_handler);

static void vue_demo_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!s_vue_ready) {
		vue.setRotation(3);
		vue.setTextWrap(false);
		vue.setFont(&Org_01);
		vue.setCurrentMode(eVueGlobalScreenDEBUG);
		s_vue_ready = true;
	}

	vue.refresh();

	printk("vue_demo: %u pixels set, w=%d h=%d rotation=%d\n",
	       vue.countSetPixels(), vue.width(), vue.height(), vue.getRotation());

	gfx_demo_push_buffer(vue.getBuffer(), vue.getBufferSize());

	k_work_schedule(&vue_demo_work, K_MSEC(VUE_DEMO_REFRESH_MS));
}

void vue_demo_start(void)
{
	k_work_schedule(&vue_demo_work, K_MSEC(VUE_DEMO_REFRESH_MS));
}
