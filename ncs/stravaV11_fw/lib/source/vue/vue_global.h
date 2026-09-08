/*
 * GFX port Phase D: the global `vue` object MenuObjects.cpp/Menuable.cpp
 * (and, later, the ported screen classes) draw into. stravaV10's own
 * Model.h declared this as `extern Vue vue;` (Vue being the full
 * VueCRS/VueFEC/VuePRC/VueDebug/Menuable god-class) -- deliberately not
 * reintroduced whole here. MenuObjects.cpp's actual usage of `vue`
 * (setCursor/setTextSize/print/println/getCursorX/getCursorY/
 * fillRoundRect) is entirely inherited Adafruit_GFX/Print API, plus
 * fillRoundRect() which Adafruit_GFX already provides a working default
 * for (built from fillRect()/fillCircleHelper() -- so it's already
 * buffer-native-accelerated for its rectangular middle section, via
 * ZephyrGFX's Phase B drawFastVLine()/fillRect() overrides). A plain
 * ZephyrGFX instance is therefore sufficient for this phase -- no
 * `Vue`-specific subclass needed yet.
 */

#ifndef SOURCE_VUE_VUE_GLOBAL_H_
#define SOURCE_VUE_VUE_GLOBAL_H_

#include "ZephyrGFX.h"

extern ZephyrGFX vue;

#endif /* SOURCE_VUE_VUE_GLOBAL_H_ */
