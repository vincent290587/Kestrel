/*
 * GFX port Phase D: the global `vue` object MenuObjects.cpp/Menuable.cpp/
 * VueDebug.cpp/VueGPS.cpp (and, later, the remaining ported screen
 * classes) draw into. stravaV10's own Model.h declared this as
 * `extern Vue vue;` (Vue being the full VueCRS/VueFEC/VuePRC/VueDebug/
 * NotifiableDevice/Menuable god-class) -- deliberately not reintroduced
 * whole here.
 *
 * `vue`'s type is `VueBase`, not plain `ZephyrGFX`: VueFEC.cpp's real
 * `vue.addNotif(...)` call is the first thing in this port that needs
 * NotifiableDevice specifically (Menuable/VueDebug/VueGPS only ever
 * needed inherited Adafruit_GFX/Print API, plus fillRoundRect(), which
 * Adafruit_GFX already provides a working default for -- see the
 * previous version of this comment). `VueBase` is a small, deliberate
 * stand-in for what the eventual `Vue` class provides -- ZephyrGFX's
 * buffer-native rendering plus NotifiableDevice's notification queue --
 * without the full VueCRS/VueFEC/VuePRC/VueDebug/Menuable diamond
 * hierarchy. Notifications queued via addNotif() aren't drawn by
 * anything yet (that's Vue::refresh()'s job in the original, not ported
 * until Vue itself is); they just sit harmlessly in the queue for now.
 */

#ifndef SOURCE_VUE_VUE_GLOBAL_H_
#define SOURCE_VUE_VUE_GLOBAL_H_

#include "ZephyrGFX.h"
#include "Notif.h"

class VueBase : public ZephyrGFX, public NotifiableDevice {
};

extern VueBase vue;

#endif /* SOURCE_VUE_VUE_GLOBAL_H_ */
