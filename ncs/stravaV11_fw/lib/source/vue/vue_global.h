/*
 * GFX port Phase D: the global `vue` object MenuObjects.cpp/Menuable.cpp/
 * VueDebug.cpp/VueGPS.cpp/VueFEC.cpp/VuePRC.cpp/VueCRS.cpp draw into.
 * stravaV10's own Model.h declared this as `extern Vue vue;` -- now
 * genuinely true here too: `vue`'s type is the real, fully-assembled
 * `Vue` class (Vue.h), not a placeholder. Every `vue.print()`/
 * `vue.setCursor()`/etc. call made from *within* one of Vue's own base
 * classes' methods (VueDebug::displayDebug(), VueFEC::tasksFEC(), ...)
 * now refers to the exact same object executing that method when it's
 * `vue` doing the calling -- matching stravaV10's own real runtime
 * behavior exactly (there, `vue` and `this` are always the same object
 * too), not just a superficially-similar stand-in the way the earlier
 * `VueBase` placeholder was.
 *
 * `VueBase` (the earlier ZephyrGFX+NotifiableDevice-only stand-in this
 * comment used to describe) is gone -- Vue itself now provides
 * everything it did and more.
 */

#ifndef SOURCE_VUE_VUE_GLOBAL_H_
#define SOURCE_VUE_VUE_GLOBAL_H_

#include "Vue.h"

extern Vue vue;

#endif /* SOURCE_VUE_VUE_GLOBAL_H_ */
