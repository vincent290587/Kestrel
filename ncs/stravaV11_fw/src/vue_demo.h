#ifndef VUE_DEMO_H_
#define VUE_DEMO_H_

#ifdef __cplusplus
extern "C" {
#endif

/* GFX port Phase D (display arbitration): periodically (1Hz, same cadence
 * as the earlier sensor_screen_demo.c) calls the real `vue.refresh()`
 * (Vue.cpp) and pushes its buffer to the real display via
 * gfx_demo_push_buffer() -- see vue_demo.cpp's own top-of-file note for
 * why this, not sensor_screen_demo.c's gfx_demo_show_sensors() screen
 * (never actually wired into main()'s boot sequence to begin with -- see
 * that note), is what now owns the live display. Call once, after
 * gfx_demo()'s own one-shot boot splash. */
void vue_demo_start(void);

/* Manual console-only screen switch (2026-09-12, "VUE LAP"/"VUE DEBUG" in
 * cmd_console.c) -- no buttons are wired to Vue's own eButtonsEvent input
 * yet, so this is the only way to change the live screen right now.
 * `mode` is an eVueGlobalScreenModes value (Vue.h) -- passed as a plain
 * int so this header stays C-includable from cmd_console.c without
 * pulling in the whole C++ Vue class hierarchy. The two macros below
 * must stay in sync with Vue.h's own enum values (vue_demo.cpp's
 * VUE_DEMO_STATIC_ASSERT catches a mismatch at compile time). */
#define VUE_MODE_DEBUG 3
#define VUE_MODE_LAP   4

void vue_demo_set_mode(int mode);

#ifdef __cplusplus
}
#endif

#endif /* VUE_DEMO_H_ */
