/*
 * GFX port Phase D: VueDebug.cpp's one hardware-sourced data need (the
 * STC3100 fuel-gauge current/voltage reading) isn't satisfiable the same
 * way between the two apps -- there's no STC3100 C++ class or Zephyr
 * driver reachable from native_sim at all, while stravaV11_fw has real
 * getters (stc3100_demo.h, Phase 3/11). Same "shared interface, stub vs.
 * real adapter" pattern as adapters/fram_stub.c vs. fram_zephyr.c.
 * Deliberately NOT routed through model_glue.h (Phase C): that
 * abstraction's job is per-field staleness for a live data screen; this
 * is a raw diagnostic dump reading current/voltage directly, matching
 * stravaV10's own VueDebug -- so it reads stc3100_demo's getters
 * directly on stravaV11_fw, same as the original read `stc` directly.
 */

#ifndef SOURCE_VUE_DEBUG_SCREEN_DATA_H_
#define SOURCE_VUE_DEBUG_SCREEN_DATA_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool debug_screen_get_stc_current_ma(float *current_ma);
bool debug_screen_get_stc_voltage_v(float *voltage_v);

/* stravaV10's original reads mes_segments.size() directly (ListeSegments,
 * source/routes/Segment.h) -- ported into stravaV11_app (Phase 1) but
 * never into stravaV11_fw at all, so this goes through the same
 * shared-interface pattern as the STC3100 reading above rather than
 * pulling in the whole routes/ tier just for one integer. Always 0 for
 * now on both apps -- real route/segment loading isn't wired into either
 * yet, so this is an honest count, not a stub pretending otherwise. */
uint32_t debug_screen_get_segment_count(void);

#ifdef __cplusplus
}
#endif

#endif /* SOURCE_VUE_DEBUG_SCREEN_DATA_H_ */
